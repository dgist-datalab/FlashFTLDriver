#include "compression_mapping.h"
#include "../../../include/search_template.h"
#include "../../../include/debug_utils.h"
#define GET_COMP_ENT(PD) (compression_ent*)(PD)
//#define REAL_COMPRESSION
static bool start_flag=true;
static LRU *comp_lru;
static uint64_t target_mem_bit;
static uint64_t now_mem_bit;
fdriver_lock_t cache_lock;
static bool compression_df;
extern uint32_t test_key;

#ifdef AMF
	extern char **mem_pool;
#endif

enum{
	COMP_READ_DATA, COMP_READ_MAP
};

#define CHUNKSIZE 512

map_function* compression_init(uint32_t contents_num, float fpr, uint64_t total_bit){
	if(start_flag){
#ifdef AMF
		printf("LOWER_MEM_DEV!\n");
#endif
		/*make lru list*/
		lru_init(&comp_lru, NULL, NULL);
		fdriver_mutex_init(&cache_lock);
		target_mem_bit=total_bit;
		printf("target_mem_bit:%u\n", target_mem_bit);
		start_flag=false;
	}

	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=compression_insert;
	res->query=compression_query;
	res->oob_check=compression_oob_check;
	res->query_retry=compression_retry;
	res->query_done=map_default_query_done;
	res->make_done=compression_make_done;
	res->get_memory_usage=compression_get_memory_usage;
	res->show_info=NULL;
	res->free=compression_free;
	//res->get_pending_request=compression_get_pending_request;

	compression_ent *comp_ent=(compression_ent*)malloc(sizeof(compression_ent));
	comp_ent->list_entry=NULL;
	comp_ent->comp_data=NULL;
	comp_ent->temp_data=NULL;
	comp_ent->pending_req=new std::vector<request*>();

	res->private_data=(void*)comp_ent;
	return res;
}

uint32_t compression_insert(map_function *m, uint32_t lba, uint32_t offset){
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	if(m->now_contents_num==0){
		comp_ent->temp_data=(uint8_t*)malloc(PAGESIZE);
		memset(comp_ent->temp_data, -1, PAGESIZE);
	}
	uint32_t idx=m->now_contents_num;
	summary_pair* pair_ptr=(summary_pair*)comp_ent->temp_data;
	pair_ptr[idx].lba=lba;
	pair_ptr[idx].piece_ppa=offset;
	map_increase_contents_num(m);
	return INSERT_SUCCESS;
}

uint32_t get_comp_lba_from_offset(compressed_form *comp_data, uint32_t i){
	uint32_t entry_per_chunk=(CHUNKSIZE-32)/comp_data->bit_num+1;
	uint32_t chunk_idx=i/entry_per_chunk;
	uint32_t offset=i%entry_per_chunk;

	uint8_t* data_ptr=&comp_data->data[chunk_idx*CHUNKSIZE/8];
	if(offset==0){
		return *(uint32_t*)data_ptr;
	}
	else{
		uint32_t head_lba=*(uint32_t*)data_ptr;
		data_ptr+=4;
		for(uint32_t i=0; i<offset; i++){
			uint32_t temp;
			switch (comp_data->bit_num){
			case 8:
				head_lba+=*(uint8_t*)data_ptr;
				data_ptr+=1;
				break;	
			case 16:
				head_lba+=*(uint16_t*)data_ptr;
				data_ptr+=2;
				break;	
			case 24:
				memcpy(&temp, data_ptr, 3);
				temp>>=8;
				head_lba+=temp;
				data_ptr+=3;
				break;
			case 32:
				head_lba+=*(uint32_t*)data_ptr;
				data_ptr+=4;
				break;
			default:
				break;
			}
		}
		return head_lba;
	}
}

compressed_form *compression_data(uint8_t *data, uint64_t *target_bit, uint32_t intra_offset){
	summary_pair *map=(summary_pair*)&data[LPAGESIZE*intra_offset];
	if(compression_df){
		//printf("break!\n");
	}
	uint32_t max_entry_num=0;
	uint32_t max_delta=0;
	uint32_t prev_lba=0;
	for(uint32_t i=0; i<LPAGESIZE/sizeof(summary_pair) && map[i].lba!=UINT32_MAX; i++){
		max_entry_num++;
		if(i==0){
			prev_lba=map[i].lba;
		}
		else{
			uint32_t temp_delta=map[i].lba-prev_lba;
			if(max_delta < temp_delta){
				max_delta=temp_delta;
			}
			prev_lba=map[i].lba;
		}
		if(compression_df){
			printf("%u %u\n", i, map[i].lba);
		}
	}

	compressed_form *res=(compressed_form*)malloc(sizeof(compressed_form));
	uint32_t chunk_num=0;
	uint32_t entry_per_chunk=0;
	if(max_entry_num!=1){
		uint32_t bit_num=0;
		while(max_delta){
			bit_num++;
			max_delta/=2;
		}
		uint32_t real_bit_num=bit_num;
		bit_num=CEIL(bit_num, 8)*8; //byte align

		entry_per_chunk=((CHUNKSIZE-32)/bit_num+1);
		chunk_num=CEIL(max_entry_num, entry_per_chunk);

		res->data_size=chunk_num*CHUNKSIZE;
		res->bit_num=bit_num;
	}
	else{
		entry_per_chunk=1;
		chunk_num=1;
		res->bit_num=0;
		res->data_size=CHUNKSIZE;
	}
	res->max_entry_num=max_entry_num;
#ifdef REAL_COMPRESSION
	res->data=(uint8_t*)malloc(res->data_size/8);
#else
	res->data=(uint8_t*)malloc(PAGESIZE/2);
#endif
	memset(res->data, -1, res->data_size/8);

	*target_bit=res->data_size;
	//printf("%u %2.f\n", res->data_size, (double)res->data_size/max_entry_num);
#ifdef REAL_COMPRESSION
	uint32_t map_idx=0;
	uint32_t data_idx=0;
	for(uint32_t chunk_idx=0; chunk_idx <chunk_num; chunk_idx++){
		map_idx=chunk_idx*entry_per_chunk;
		uint32_t data_ptr=chunk_idx*CHUNKSIZE/8;
		uint8_t *des_ptr=(uint8_t *)&res->data[data_ptr];
		for(uint32_t i=0; i<entry_per_chunk; i++){
			if(map[map_idx].lba==test_key){
				//GDB_MAKE_BREAKPOINT;
			}
			if(i==0){
				((uint32_t*)des_ptr)[0]=map[map_idx].lba;
				des_ptr+=sizeof(uint32_t);
			}
			else{
				uint32_t delta=map[map_idx].lba-prev_lba;
				switch(res->bit_num){
					case 8:
						*(uint8_t*)des_ptr=delta;
						des_ptr+=1;
						break;
					case 16:
						*(uint16_t*)des_ptr=delta;
						des_ptr+=2;
						break;
					case 24:
	//					delta<<=8;
						memcpy(des_ptr, &delta, 3);
						des_ptr+=3;
						break;
					case 32:
						*(uint32_t*)des_ptr=delta;
						des_ptr+=4;
						break;
					default:
						abort();
						break;
				}
			}
			prev_lba=map[map_idx].lba;
			map_idx++;
			if(map_idx==max_entry_num){
				goto done;
			}
		}
	}
done:
#else
	for(uint32_t i=0; i<max_entry_num; i++){
		((uint32_t*)res->data)[i]=map[i].lba;
	}
#endif


	/*debug check
	for(uint32_t i=0; i<max_entry_num; i++){
		printf("%u %u\n", map[i].lba, get_comp_lba_from_offset(res, i));
	}*/

	return res;
}

int comp_pair_cmp(uint32_t p, uint32_t target){
	if(p < target) return -1;
	else if(p>target) return 1;
	return 0;
}

uint32_t find_offset(uint32_t lba, compressed_form *comp_data){
	//printf("find lab:%u\n", lba);

	if(lba==test_key){
		//GDB_MAKE_BREAKPOINT;
	}
	uint32_t target_idx=0;
#ifdef REAL_COMPRESSION
	uint32_t s=0, e=comp_data->data_size/CHUNKSIZE-1;
	uint32_t last_entry=e;
	uint8_t* data_ptr=comp_data->data;
	uint32_t mid=0;
	while(s<=e){
		mid=(s+e)/2;
		uint32_t target_lba=*(uint32_t*)&data_ptr[mid*CHUNKSIZE/8]; //check head
		if(mid==last_entry){
			if(target_lba <=lba){
				break; //find target;
			}
			else{
				e=mid-1;
			}
		}
		else{
			uint32_t target_end_lba=*(uint32_t*)&data_ptr[(mid+1)*CHUNKSIZE/8];
			if(target_lba<= lba && target_end_lba>lba){
				break;
			}
			else{
				if(target_lba > lba){
					e=mid-1;
				}
				else{
					s=mid+1;
				}
			}
		}
	}

	data_ptr=&comp_data->data[mid*CHUNKSIZE/8];
	uint32_t head_lba=*(uint32_t*)&data_ptr[0];
	uint32_t entry_per_chunk=0;
	if(comp_data->max_entry_num!=1){
		entry_per_chunk=((CHUNKSIZE-32)/comp_data->bit_num+1);
	}
	else{
		entry_per_chunk=1;
	}
	target_idx=entry_per_chunk*mid;
	for(uint32_t i=0; i<=entry_per_chunk; i++){
		if(i==0){
			if(head_lba==lba){
				break;
			}
			else{
				data_ptr+=sizeof(uint32_t);
			}
		}
		else{
			uint32_t temp=0;
			switch (comp_data->bit_num){
			case 8:
				head_lba+=*(uint8_t*)data_ptr;
				break;	
			case 16:
				head_lba+=*(uint16_t*)data_ptr;
				break;	
			case 24:
				memcpy(&temp, data_ptr, 3);
				head_lba+=temp;
				break;
			case 32:
				head_lba+=*(uint32_t*)data_ptr;
				break;
			default:
				break;
			}

			if(head_lba==lba){
				target_idx+=i;
				break;
			}
			data_ptr+=comp_data->bit_num/8;
		}
		if(i==entry_per_chunk){
			return NOT_FOUND;
		}
	}
#else
	uint32_t *temp_target=(uint32_t*)comp_data->data;
	bs_search(temp_target, 0, comp_data->max_entry_num, lba, comp_pair_cmp, target_idx);
#endif
	//printf("%u target_idx:%u\n", lba, target_idx);
	return target_idx;
}

uint32_t compression_query(map_function *m, uint32_t lba, map_read_param ** param){
	fdriver_lock(&cache_lock);
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	map_read_param *res_param=(map_read_param*)calloc(1, sizeof(map_read_param));
	res_param->lba=lba;
	res_param->mf=m;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;

	if(comp_ent->list_entry){ //in cache
		res_param->retry_flag=COMP_READ_DATA;
		res_param->read_map=false;
		uint32_t res=find_offset(lba, comp_ent->comp_data);
	//	printf("comp_lru->size:%u\n", comp_lru->size);
		if(comp_lru->size==300){
		//	printf("break!\n");
		}
	//	lru_check_error(comp_lru);
		lru_update(comp_lru, (lru_node*)comp_ent->list_entry);
		fdriver_unlock(&cache_lock);
		return res;
	}
	else{//no cache
		static int miss_cnt=0;
		fdriver_unlock(&cache_lock);
		res_param->retry_flag=COMP_READ_MAP;
		res_param->read_map=true;
		if(comp_ent->flag==NO_PENDING){
			comp_ent->flag=FLYING;
			return READ_MAP;
		}
		else{
			return READ_MAP;
		}
		//static uint32_t miss_cnt=0;
		//printf("miss: %u\n", miss_cnt++);
	}
}

uint32_t compression_oob_check(map_function *m, map_read_param *param){
	if(param->retry_flag==COMP_READ_MAP){
	//	printf("[oob]%u --> %p : %c\n", param->p_req->key, param->p_req->value->value, param->p_req->value->value[0]);
		return NOT_FOUND;
	}
	else{
		return map_default_oob_check(m, param);	
	}
}

uint32_t compression_retry(map_function *m, map_read_param *param){
	fdriver_lock(&cache_lock);
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	param->retry_flag=COMP_READ_DATA;
	//printf("[retry]%u --> %p : %c\n", param->p_req->key, param->p_req->value->value, param->p_req->value->value[0]);

	if(comp_ent->flag==FLYING){
		comp_ent->flag=NO_PENDING;
	}

	if(comp_ent->list_entry){ //the prev request may insert data to cache.
		/*promote cache*/
		uint32_t res=find_offset(param->lba, comp_ent->comp_data);
		lru_update(comp_lru, (lru_node*)comp_ent->list_entry);
		//lru_check_error(comp_lru);
		fdriver_unlock(&cache_lock);
		return 	res;
	}

#ifdef AMF
	uint32_t temp_ppa=param->psa/L2PGAP;
	comp_ent->comp_data=compression_data((uint8_t*)mem_pool[temp_ppa], &comp_ent->mem_bit, param->intra_offset);
#else
	comp_ent->comp_data=compression_data((uint8_t*)param->p_req->value->value, &comp_ent->mem_bit, param->intra_offset);
#endif
	compression_df=false;

	/*check cache full*/
	static int eviction_cnt=0;
	compression_ent *temp;
	while(now_mem_bit+comp_ent->mem_bit > target_mem_bit){
		temp=(compression_ent*)lru_pop(comp_lru);
		//lru_check_error(comp_lru);
		now_mem_bit-=temp->mem_bit;
		if(now_mem_bit > target_mem_bit){
			abort();
		}
		free(temp->comp_data->data);
		free(temp->comp_data);
		temp->list_entry=NULL;
		temp->mem_bit=0;
//		printf("%u %u %u\n",target_mem_bit, eviction_cnt++, comp_lru->size);
	}

	comp_ent->list_entry=lru_push(comp_lru, (void*) comp_ent);
	//lru_check_error(comp_lru);
	now_mem_bit+=comp_ent->mem_bit;
	uint32_t res= find_offset(param->lba, comp_ent->comp_data);
	fdriver_unlock(&cache_lock);

	return res;
}

uint64_t compression_get_memory_usage(map_function *m, uint32_t target_bit){
	fdriver_lock(&cache_lock);
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	fdriver_unlock(&cache_lock);
	if(!comp_ent){
		return NULL;
	}
	return comp_ent->mem_bit;
}

void compression_make_done(map_function *m){
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	comp_ent->comp_data=compression_data(comp_ent->temp_data, &comp_ent->mem_bit, 0);
	
	free(comp_ent->temp_data);

	fdriver_lock(&cache_lock);
	compression_ent *temp;
	static int eviction_cnt=0;
	while(now_mem_bit+comp_ent->mem_bit > target_mem_bit){
		temp=(compression_ent*)lru_pop(comp_lru);
		//lru_check_error(comp_lru);
		now_mem_bit-=temp->mem_bit;
		if(now_mem_bit > target_mem_bit){
			abort();
		}	
		free(temp->comp_data->data);
		free(temp->comp_data);
		temp->list_entry=NULL;
		temp->mem_bit=0;
	//	printf("%u %u %u\n",target_mem_bit, eviction_cnt++, comp_lru->size);
	}

	comp_ent->list_entry=lru_push(comp_lru, (void*) comp_ent);
	now_mem_bit+=comp_ent->mem_bit;
	fdriver_unlock(&cache_lock);
	return;
}

void compression_free(map_function *m){
	fdriver_lock(&cache_lock);
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	if(comp_ent->list_entry){
		lru_delete(comp_lru, (lru_node*)comp_ent->list_entry);
	//	printf("comp_lru->size:%u\n", comp_lru->size);
		now_mem_bit-=comp_ent->mem_bit;
		if(now_mem_bit > target_mem_bit){
			abort();
		}
		comp_ent->list_entry=NULL;
		if(comp_ent->pending_req->size()){
			printf("??????\n");
			abort();
		}
		delete comp_ent->pending_req;
	}
	fdriver_unlock(&cache_lock);

	free(comp_ent);
}

bool compression_add_pending_req(map_function *m, request *req){

}

request *compression_get_pending_request(map_function *m){

}
