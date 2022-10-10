#include "compression_mapping.h"
#include "../../../include/search_template.h"
#define GET_COMP_ENT(PD) (compression_ent*)(PD)
static bool start_flag=true;
static LRU *comp_lru;
static uint64_t target_mem_bit;
static uint64_t now_mem_bit;
fdriver_lock_t cache_lock;

enum{
	COMP_READ_DATA, COMP_READ_MAP
};

#define CHUNKSIZE 512

map_function* compression_init(uint32_t contents_num, float fpr, uint64_t total_bit){
	if(start_flag){
		/*make lru list*/
		lru_init(&comp_lru, NULL, NULL);
		fdriver_mutex_init(&cache_lock);
		target_mem_bit=total_bit;
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

	compression_ent *comp_ent=(compression_ent*)malloc(sizeof(compression_ent));
	comp_ent->list_entry=NULL;
	comp_ent->comp_data=NULL;

	res->private_data=(void*)comp_ent;
	return res;
}

uint32_t compression_insert(map_function *m, uint32_t lba, uint32_t offset){
	return INSERT_SUCCESS;
}

compressed_form *compression_data(uint8_t *data, uint64_t *target_bit, uint32_t intra_offset){
	summary_pair *map=(summary_pair*)&data[LPAGESIZE*intra_offset];
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
	}

	uint32_t bit_num=0;
	while(max_delta){
		bit_num++;
		max_delta/=2;
	}
	bit_num=CEIL(bit_num, 8)*8; //byte align

	compressed_form *res=(compressed_form*)malloc(sizeof(compressed_form));
	uint32_t entry_per_chunk=(CHUNKSIZE-32)/bit_num;
	uint32_t chunk_num=CEIL(max_entry_num, entry_per_chunk);
	res->data_size=chunk_num*CHUNKSIZE;
	res->data=(uint32_t*)malloc(PAGESIZE/2);
	res->max_entry_num=max_entry_num;
	memset(res->data, -1, PAGESIZE/2);

	*target_bit=res->data_size;
	//printf("%u %2.f\n", res->data_size, (double)res->data_size/max_entry_num);

	/* skip compression
	uint32_t map_idx=0;
	uint32_t data_idx=0;
	uint32_t prev_lba;
	for(uint32_t chunk_idx=0; chunk_idx <chunk_num; chunk_idx++){
		uint8_t *ptr=&res->data[chunk_idx*CHUNKSIZE];
		for(uint32_t i=0; i<entry_per_chunk; i++){
			if(map_idx==max_entry_num){
				goto done;
			}
			if(i=0){
				prev_lba=map[map_idx].lba;
				*((uint32_t*)ptr)=map[map_idx].lba;
				data_idx+=sizeof(uint32_t);
			}
			else{
				uint32_t delta=map[map_idx].lba-prev_lba;
				data_idx+=bit_num;
			}
			map_idx++;
		}
	}
	done:
	*/

	for(uint32_t i=0; i<max_entry_num; i++){
		res->data[i]=map[i].lba;
	}

	return res;
}

int comp_pair_cmp(uint32_t p, uint32_t target){
	if(p < target) return -1;
	else if(p>target) return 1;
	return 0;
}

uint32_t find_offset(uint32_t lba, compressed_form *comp_data){
	uint32_t target_idx=0;
	bs_search(comp_data->data, 0, comp_data->max_entry_num, lba, comp_pair_cmp, target_idx);
	return target_idx;
}

uint32_t compression_query(map_function *m, uint32_t lba, map_read_param ** param){
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	map_read_param *res_param=(map_read_param*)calloc(1, sizeof(map_read_param));
	res_param->lba=lba;
	res_param->mf=m;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;

	fdriver_lock(&cache_lock);
	if(comp_ent->list_entry){ //in cache
		res_param->retry_flag=COMP_READ_DATA;
		res_param->read_map=false;
		lru_update(comp_lru, (lru_node*)comp_ent->list_entry);
		uint32_t res=find_offset(lba, comp_ent->comp_data);
		fdriver_unlock(&cache_lock);
		return res;
	}
	else{//no cache
		fdriver_unlock(&cache_lock);
		//static uint32_t miss_cnt=0;
		//printf("miss: %u\n", miss_cnt++);
		res_param->retry_flag=COMP_READ_MAP;
		res_param->read_map=true;
		return READ_MAP;
	}
}

uint32_t compression_oob_check(map_function *m, map_read_param *param){
	if(param->retry_flag==COMP_READ_MAP){
		return NOT_FOUND;
	}
	else{
		return map_default_oob_check(m, param);	
	}
}

uint32_t compression_retry(map_function *m, map_read_param *param){
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	param->retry_flag=COMP_READ_DATA;

	fdriver_lock(&cache_lock);
	if(comp_ent->list_entry){ //the prev request may insert data to cache.
		/*promote cache*/
		uint32_t res=find_offset(param->lba, comp_ent->comp_data);
		lru_update(comp_lru, (lru_node*)comp_ent->list_entry);
		fdriver_unlock(&cache_lock);
		return 	res;
	}
	fdriver_unlock(&cache_lock);

	comp_ent->comp_data=compression_data((uint8_t*)param->p_req->value->value, &comp_ent->mem_bit, param->intra_offset);

	/*check cache full*/
	fdriver_lock(&cache_lock);
	static int eviction_cnt=0;
	compression_ent *temp;
	while(now_mem_bit+comp_ent->mem_bit > target_mem_bit){
		temp=(compression_ent*)lru_pop(comp_lru);
		now_mem_bit-=temp->mem_bit;
		
		free(temp->comp_data->data);
		free(temp->comp_data);
		temp->list_entry=NULL;
		temp->mem_bit=0;
		//printf("%u %u %u\n",target_mem_bit, eviction_cnt++, comp_lru->size);
	}

	comp_ent->list_entry=lru_push(comp_lru, (void*) comp_ent);
	now_mem_bit+=comp_ent->mem_bit;
	uint32_t res= find_offset(param->lba, comp_ent->comp_data);
	fdriver_unlock(&cache_lock);

	return res;
}

uint64_t compression_get_memory_usage(map_function *m, uint32_t target_bit){
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	return comp_ent->mem_bit;
}

void compression_make_done(map_function *m){
	return;
}

void compression_free(map_function *m){
	compression_ent *comp_ent=GET_COMP_ENT(m->private_data);
	fdriver_lock(&cache_lock);
	if(comp_ent->list_entry){
		lru_delete(comp_lru, (lru_node*)comp_ent->list_entry);
		now_mem_bit-=comp_ent->mem_bit;
		if(now_mem_bit > target_mem_bit){
			abort();
		}
	}
	fdriver_unlock(&cache_lock);

	free(comp_ent);
}