#include "version.h"
#include "../../include/settings.h"
#include "lsmtree.h"
#include <math.h>

extern lsmtree LSM;

version *version_init(uint8_t max_valid_version_num, uint32_t LBA_num){
	version *res=(version*)malloc(sizeof(version));
	res->start_hand=res->end_hand=0;
	res->key_version=(uint8_t*)calloc(LBA_num, sizeof(uint8_t));
	memset(res->key_version, -1, sizeof(uint8_t)*LBA_num);
	res->valid_version_num=0;
	res->max_valid_version_num=max_valid_version_num;

	res->ridx_empty_queue=new std::queue<uint32_t>();
	printf("max_version idx:%u\n", max_valid_version_num-1);
	for(uint32_t i=0; i<max_valid_version_num; i++){
		res->ridx_empty_queue->push(i);
	}

	res->version_invalidation_cnt=(uint32_t*)calloc(TOTALRUNIDX+1, sizeof(uint32_t));
	res->version_early_invalidate=(bool*)calloc(TOTALRUNIDX+1, sizeof(bool));
	res->ridx_populate_queue=new std::queue<uint32_t>();
	res->memory_usage_bit=ceil(log2(max_valid_version_num))*LBA_num;
	res->poped_version_num=0;
	return res;
}

uint32_t version_get_empty_ridx(version *v){
	if(v->ridx_empty_queue->empty()){
		EPRINT("should merge before empty ridx", true);
	}
	uint32_t res=v->ridx_empty_queue->front();
	v->ridx_empty_queue->pop();
	return res;
}

void version_get_merge_target(version *v, uint32_t *ridx_set){
	for(uint32_t i=0; i<2; i++){
		ridx_set[i]=v->ridx_populate_queue->front();
		v->ridx_populate_queue->pop();
	}
}

void version_unpopulate_run(version *v, uint32_t ridx){
	v->ridx_empty_queue->push(ridx);
}

void version_populate_run(version *v, uint32_t ridx){
	v->ridx_populate_queue->push(ridx);
}

void version_sanity_checker(version *v){
	uint32_t remain_empty_size=v->ridx_empty_queue->size();
	uint32_t populate_size=v->ridx_populate_queue->size();
	if(remain_empty_size+populate_size!=v->max_valid_version_num){
		printf("error log : empty-size(%d) populate-size(%d)\n", remain_empty_size, populate_size);
		EPRINT("version sanity error", true);
	}
}

void version_free(version *v){
	delete v->ridx_empty_queue;
	delete v->ridx_populate_queue;
	free(v->key_version);
	free(v);
}
extern uint32_t debug_lba;
void version_coupling_lba_ridx(version *v, uint32_t lba, uint8_t ridx){
	if(ridx>TOTALRUNIDX){
		EPRINT("over version num", true);
	}
	if(lba==debug_lba){
		printf("[version_map] lba:%u->%u\n",lba, ridx);
	}
	if(v->key_version[lba]!=UINT8_MAX){
		v->version_invalidation_cnt[v->key_version[lba]]++;
	}
	v->key_version[lba]=ridx;
}


void version_reinit_early_invalidation(version *v, uint32_t ridx_num, uint32_t *ridx){
	for(uint32_t i=0; i<ridx_num; i++){
		v->version_invalidation_cnt[ridx[i]]=0;
		v->version_early_invalidate[ridx[i]]=false;
	}
}

static bool early_invalidate_available_check(uint32_t ridx){
	run *r=&LSM.disk[LSM.param.LEVELN-1]->array[ridx];
	sst_file *sptr;
	map_range *mptr;
	uint32_t sidx, midx;
	for_each_sst(r, sptr, sidx){
		for_each_map_range(sptr, mptr, midx){
			if(LSM.gc_unavailable_seg[mptr->ppa/_PPS]){
				return false;
			}
		}
	}
	return true;
}

uint32_t version_get_max_invalidation_target(version *v, uint32_t *invalidated_num, uint32_t *avg_invalidated_num){
	uint32_t target_ridx=UINT32_MAX;
	uint32_t target_invalidation_cnt=0;
	for(uint32_t i=0; i<v->max_valid_version_num; i++){
		if(v->version_early_invalidate[i]) continue;
		if(target_invalidation_cnt<v->version_invalidation_cnt[i]){
			if(early_invalidate_available_check(i)){
				target_ridx=i;
				target_invalidation_cnt=v->version_invalidation_cnt[i];
			}	
		}
	}
	if(target_ridx==UINT32_MAX){
		return target_ridx;
	}

	if(invalidated_num){
		*invalidated_num=target_invalidation_cnt;
	}
	if(avg_invalidated_num){
		*avg_invalidated_num=(target_invalidation_cnt/
			LSM.disk[LSM.param.LEVELN-1]->array[target_ridx].now_sst_file_num);
	}

	return target_ridx;
}
