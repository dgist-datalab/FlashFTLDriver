#include "lsmtree.h"
#include "compaction.h"
#include "../../bench/bench.h"
#include "nocpy.h"

#define ALL_HEAD_READ_T 1
#define OBO_HEAD_READ_T 2

#define MEMTABLE 0
#define TEMPENT 1
#define RETRY 2
#define CACHE 3

void *lsm_range_end_req(algo_req *const req);
uint32_t __lsm_range_get_sub(request* req,lsm_sub_req *sr,void *arg1, void *arg2, uint8_t type);
extern lsmtree LSM;

uint32_t lsm_multi_set(request *const req, uint32_t num){
	compaction_check(req->key,false);
	for(int i=0; i<req->num; i++){
		skiplist_insert(LSM.memtable,req->multi_key[i],req->multi_value[i],true);
	}
	req->end_req(req);

	if(LSM.memtable->size>=LSM.KEYNUM)
		return 1;
	else 
		return 0;
}


algo_req *lsm_range_get_req_factory(request *parents,lsm_range_params *params,uint8_t type){
	algo_req *lsm_req=(algo_req*)malloc(sizeof(algo_req));
	params->lsm_type=type;
	lsm_req->params=(void*)params;
	lsm_req->parents=parents;
	lsm_req->end_req=lsm_range_end_req;
	lsm_req->type_lower=0;
	lsm_req->rapid=true;
	lsm_req->type=type;
	return lsm_req;
}
#ifdef BLOOM
uint8_t lsm_filter_checking(BF *filter, lsm_sub_req *sr_sets,request *req){
	for(int i=0; i<req->num; i++){
		if(bf_check(filter,sr_sets[i].key))
			return 1;
	}
	return 0;
}
#endif

uint32_t lsm_range_get(request *const req){
	uint32_t res_type=0;
	lsm_proc_re_q();
	//bench_algo_start(req);
	res_type=__lsm_range_get(req);
	static bool first=false;
	if(!first){
		first=true;
		LSM.lop->print_level_summary();
	}
	if(res_type==0){
		req->type=FS_NOTFOUND_T;
		req->end_req(req);
	}
	return res_type;
}
int cnt3;
static int retry;
void *lsm_range_end_req(algo_req *const req){
	lsm_range_params* range_params=(lsm_range_params*)req->params;
	request *original_req=req->parents;
	bool header_flag=false;
	bool data_flag=false;
	switch(range_params->lsm_type){
		case DATAR:
			fdriver_lock(&range_params->global_lock);
			range_params->now++;
			if(range_params->now==range_params->max){
				data_flag=true;
			}
			fdriver_unlock(&range_params->global_lock);		
			break;
		case HEADERR:
			fdriver_lock(&range_params->global_lock);
			range_params->now++;
			if(range_params->now==range_params->max){
				header_flag=true;	
			}
			fdriver_unlock(&range_params->global_lock);
			break;
	}

	if(header_flag){
		while(!inf_assign_try(original_req)){};
		retry++;
	}
	else if(data_flag){
		int realloc_cnt=(LSM.LEVELN-LSM.LEVELCACHING) *2;
		if(original_req->num < realloc_cnt){
			for(int i=original_req->num; i<realloc_cnt; i++){
				inf_free_valueset(original_req->multi_value[i],FS_MALLOC_R);
			}
		}
		free(range_params->mapping_data);
		free(range_params);
		original_req->end_req(original_req);
	}
	free(req);
	return NULL;
}

int cnt2=0;
int cnt=0;
uint32_t __lsm_range_get_after_header(request *req){
	lsm_range_params *params=(lsm_range_params*)req->params;
	keyset_iter* level_iter[LSM.LEVELN]; //memtable
	keyset *target_keys=(keyset*)malloc(sizeof(keyset)*req->num);
	int level_mapping_cnt[LSM.LEVELN];
//	printf("cnt2:%d\n",cnt2++);
	//memtable;
	//HEADER
	snode *mem_node=skiplist_find_lowerbound(LSM.memtable,req->key);
	for(int i=0; i<LSM.LEVELN; i++){
		if(i>=LSM.LEVELCACHING && params->mapping_data[i*RANGEGETNUM]==NULL){
			level_iter[i]=NULL;
			continue;
		}
		level_iter[i]=LSM.lop->header_get_keyiter(LSM.disk[i],i<LSM.LEVELCACHING?NULL:params->mapping_data[i*RANGEGETNUM],&req->key);
		level_mapping_cnt[i]=1;
	}

	keyset min={0,0}, temp;
	bool no_memtable=false;
	for(int j=0;j<req->num; j++){
		int target=0;
		min.ppa=-1;

		for(int i=0; i<LSM.LEVELN+1; i++){
			if(i==0 && !no_memtable){
				if(mem_node==LSM.memtable->header) continue;
				if(!mem_node){
					no_memtable=true;
					continue;
				}
				min.ppa=0;
				min.lpa=mem_node->key;
				target=-1;
			}
			else{
				if(i==0) continue;
				int level=i-1;
again:
				if(level_iter[level]==NULL) continue;
				LSM.lop->header_next_key_pick(LSM.disk[level],level_iter[level],&temp);

				if(temp.ppa==UINT_MAX){ //the target header end
					if(level<LSM.LEVELCACHING || level_mapping_cnt[level]>=RANGEGETNUM) continue;
					if(params->mapping_data[level*RANGEGETNUM + level_mapping_cnt[level]]==NULL) continue;
					//it is called when the level is not caching
					if(level_iter[level]->private_data) free(level_iter[level]->private_data);
					free(level_iter[level]);
	//				printf("changed iter!\n");
					if(params->mapping_data[level*RANGEGETNUM+level_mapping_cnt[level]]==0){
						level_iter[level]=NULL;
						continue;
					}
					level_iter[level]=LSM.lop->header_get_keyiter(LSM.disk[level],params->mapping_data[level*RANGEGETNUM+level_mapping_cnt[level]++],&req->key);
					goto again;
				}

				if(min.ppa==UINT_MAX){
					min=temp;
					target=level;
				}
				else if(KEYCMP(min.lpa,temp.lpa)>0){
					target=level;
					min=temp;
				}
			}
		}

		if(min.ppa==UINT_MAX){
			params->max--;
			target_keys[j].ppa=-1;
			continue;
		}

		if(target==-1){
			mem_node=mem_node->list[1];
			params->max--;
			target_keys[j].ppa=-1;
		}else{
			target_keys[j]=LSM.lop->header_next_key(LSM.disk[target],level_iter[target]);
		}
	}
	
	algo_req *ar_req;
	int throw_req=0;
	int temp_num=req->num;
	int i;

	if(params->max==0){
		free(params->mapping_data);
		free(params);
		req->end_req(req);
		goto finish;
	}

	for(i=0; i<temp_num; i++){
		if(target_keys[i].ppa==UINT_MAX){continue;}
		throw_req++;
		ar_req=lsm_range_get_req_factory(req,params,DATAR);	
#ifdef DVALUE
		LSM.li->read(target_keys[i].ppa/NPCINPAGE,PAGESIZE,req->multi_value[i],ASYNC,ar_req);
#else
		LSM.li->read(target_keys[i].ppa,PAGESIZE,req->multi_value[i],ASYNC,ar_req);
#endif
	}
finish:
	for(i=0; i<LSM.LEVELN; i++){
		if(level_iter[i]!=NULL){
			if(level_iter[i]->private_data) free(level_iter[i]->private_data);
			free(level_iter[i]);
		}
	}
	free(target_keys);
	return 1;
}
uint32_t __lsm_range_get(request *const req){
	lsm_range_params *params;
	/*after read all headers*/
	if(req->params!=NULL){
		params=(lsm_range_params*)req->params;
		params->now=0;
		params->max=req->num;
		return __lsm_range_get_after_header(req);		
	}
	//printf("cnt:%d\n",cnt++);
	int realloc_cnt=(LSM.LEVELN-LSM.LEVELCACHING) *2;
	if(req->num < realloc_cnt){
		req->multi_value=(value_set**)realloc(req->multi_value,realloc_cnt*sizeof(value_set));
		for(int i=req->num; i<realloc_cnt; i++){
			req->multi_value[i]=inf_get_valueset(NULL,FS_GET_T,PAGESIZE);
		}
	}
	uint32_t read_header[LSM.LEVELN*RANGEGETNUM]={0,};
	/*req all headers read*/
	params=(lsm_range_params*)malloc(sizeof(lsm_range_params));
	fdriver_lock_init(&params->global_lock,1);
	params->mapping_data=(char**)calloc(sizeof(char*),LSM.LEVELN*RANGEGETNUM);
	memset(params->mapping_data,0,sizeof(char*)*LSM.LEVELN*RANGEGETNUM);
	params->now=0;
	params->max=(LSM.LEVELN-LSM.LEVELCACHING)*RANGEGETNUM;

	req->params=params; // req also has same params;
	/*Mapping read section*/
	run_t **rs;
	algo_req *ar_req;
	int use_valueset_cnt=0;
	for(int i=LSM.LEVELCACHING; i<LSM.LEVELN; i++){
		pthread_mutex_lock(&LSM.level_lock[i]);
		rs=LSM.lop->find_run_num(LSM.disk[i],req->key,RANGEGETNUM);
		pthread_mutex_unlock(&LSM.level_lock[i]);
		if(!rs){
			params->mapping_data[i]=NULL;
			params->max-=RANGEGETNUM;
			continue;
		}

		for(int j=0; rs[j]!=NULL; j++){
			if(rs[j]->c_entry){
				if(LSM.nocpy) params->mapping_data[i*RANGEGETNUM+j]=rs[j]->cache_nocpy_data_ptr;
				else{
					params->mapping_data[i*RANGEGETNUM+j]=(char*)malloc(PAGESIZE);
					memcpy(params->mapping_data[i*RANGEGETNUM+j],rs[j]->cache_data->sets,PAGESIZE);
				}
				params->max--;
				if(rs[j+1]==NULL && j+1 <RANGEGETNUM){
					params->max-=(RANGEGETNUM-(j+1));
				}
				continue;
			}
			if(rs[j+1]==NULL && j+1 <RANGEGETNUM){
				params->max-=(RANGEGETNUM-(j+1));
			}
			if(LSM.nocpy)	params->mapping_data[i*RANGEGETNUM+j]=(char*)nocpy_pick(rs[j]->pbn);
			else params->mapping_data[i*RANGEGETNUM+j]=req->multi_value[use_valueset_cnt]->value;
			read_header[use_valueset_cnt++]=rs[j]->pbn;
		}
		free(rs);
	}
	if(params->max==params->now){//all target header in level caching or caching
		__lsm_range_get(req);
	}else{
		if(use_valueset_cnt==0){
			LSM.lop->all_print();
			printf("key:%.*s\n",req->key.len,req->key.key);
			printf("%d %d(m,n)\n",params->max, params->now);
		}
		for(int i=0;i<use_valueset_cnt;i++){
			ar_req=lsm_range_get_req_factory(req,params,HEADERR);
			LSM.li->read(read_header[i],PAGESIZE,req->multi_value[i],ASYNC,ar_req);
		}
	}
	return 1;
}
