#include "version.h"
#include "../../include/settings.h"
#include "lsmtree.h"
#include <math.h>

extern lsmtree LSM;

version *version_init(uint8_t max_valid_version_num, uint8_t total_version_number, 
		uint32_t last_level_version_sidx, uint32_t LBA_num, level **disk, uint32_t leveln){
	version *res=(version*)malloc(sizeof(version));
	res->start_hand=res->end_hand=0;
	res->key_version=(uint8_t*)calloc(LBA_num, sizeof(uint8_t));
	memset(res->key_version, -1, sizeof(uint8_t)*LBA_num);
	res->valid_version_num=0;
	res->max_valid_version_num=max_valid_version_num;
	printf("max_version idx:%u\n", max_valid_version_num);

	res->ridx_empty_queue=new std::queue<uint32_t>*[leveln];
	uint32_t ridx=0;
	for(uint32_t i=0; i<leveln; i++){
		res->ridx_empty_queue[i]=new std::queue<uint32_t>();
		switch(disk[i]->level_type){
			case LEVELING:
			case LEVELING_WISCKEY:
				res->ridx_empty_queue[i]->push(ridx++);
				break;
			case TIERING:
				for(uint32_t j=0; j<LSM.param.normal_size_factor; j++){
					res->ridx_empty_queue[i]->push(ridx++);
				}
				break;
		}
	}


	res->ridx_populate_queue=new std::queue<uint32_t>*[leveln];
	for(uint32_t i=0; i<leveln; i++){
		res->ridx_populate_queue[i]=new std::queue<uint32_t>();
	}

	res->total_version_number=total_version_number;
	res->version_invalidation_cnt=(uint32_t*)calloc(res->total_version_number+1, sizeof(uint32_t));
	res->version_early_invalidate=(bool*)calloc(res->total_version_number+1, sizeof(bool));
	res->memory_usage_bit=ceil(log2(max_valid_version_num))*LBA_num;
	res->poped_version_num=0;
	fdriver_mutex_init(&res->version_lock);
	res->last_level_version_sidx=last_level_version_sidx;
	return res;
}

uint32_t version_get_empty_ridx(version *v, uint32_t level){
	if(v->ridx_empty_queue[level]->empty()){
		EPRINT("should merge before empty ridx", true);
	}
	uint32_t res=v->ridx_empty_queue[level]->front();
	v->ridx_empty_queue[level]->pop();
	return res;
}

void version_get_merge_target(version *v, uint32_t *ridx_set, uint32_t level){
	for(uint32_t i=0; i<(1+1); i++){
		ridx_set[i]=v->ridx_populate_queue[level]->front();
		v->ridx_populate_queue[level]->pop();
	}
}

void version_unpopulate_run(version *v, uint32_t ridx, uint32_t level_idx){
	v->ridx_empty_queue[level_idx]->push(ridx);
}

void version_populate_run(version *v, uint32_t ridx, uint32_t level_idx){
	v->ridx_populate_queue[level_idx]->push(ridx);
	if(level_idx==LSM.param.LEVELN-1){
		version_enable_ealry_invalidation(v,ridx);
	}
}

void version_sanity_checker(version *v){
	uint32_t remain_empty_size=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		remain_empty_size+=v->ridx_empty_queue[i]->size();
	}

	uint32_t populate_size=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		populate_size+=v->ridx_populate_queue[i]->size();
	}
	if(remain_empty_size+populate_size!=v->max_valid_version_num){
		printf("error log : empty-size(%d) populate-size(%d)\n", remain_empty_size, populate_size);
		EPRINT("version sanity error", true);
	}
}

void version_free(version *v){
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		delete v->ridx_empty_queue[i];
		delete v->ridx_populate_queue[i];
	}
	delete v->ridx_empty_queue;
	delete v->ridx_populate_queue;

	free(v->version_early_invalidate);
	free(v->version_invalidation_cnt);
	free(v->key_version);
	free(v);
}
extern uint32_t debug_lba;
void version_coupling_lba_ridx(version *v, uint32_t lba, uint8_t ridx){
	if(ridx>v->total_version_number){
		EPRINT("over version num", true);
	}
	if(lba==debug_lba){
		if(LSM.global_debug_flag){
			EPRINT("debug point", false);
		}
		printf("[version_map] lba:%u->%u\n",lba, ridx);
	}
	fdriver_lock(&v->version_lock);
	if(v->key_version[lba]!=UINT8_MAX){
		v->version_invalidation_cnt[v->key_version[lba]]++;
	}
	v->key_version[lba]=ridx;
	fdriver_unlock(&v->version_lock);
}


void version_reinit_early_invalidation(version *v, uint32_t ridx_num, uint32_t *ridx){
	for(uint32_t i=0; i<ridx_num; i++){
		v->version_invalidation_cnt[ridx[i]]=0;
		v->version_early_invalidate[ridx[i]]=false;
	}
}

uint32_t version_get_early_invalidation_target(version *v){
	/*
	static int cnt=0;
	printf("-------------%d------------------\n",cnt++);
	for(uint32_t i=0; i<v->max_valid_version_num; i++){
		printf("%u -> ridx:%u -> %s\n", i, version_to_run(v,i), 
				v->version_early_invalidate[version_to_run(v,i)]?"true":"false");
	}*/
	for(uint32_t i=0; i<v->max_valid_version_num; i++){
		if(v->version_early_invalidate[version_to_run(v,i)]){
			return version_to_run(v,i);
		}
	}
	return UINT32_MAX;
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
	for(uint32_t i=v->last_level_version_sidx; i<v->max_valid_version_num; i++){
		if(v->version_early_invalidate[i]) continue;
		if(LSM.now_merging_run[0]==i || LSM.now_merging_run[1]==i) continue;
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

uint32_t version_update_for_trivial_move(version *v, uint32_t start_lba, uint32_t end_lba, 
		uint32_t original_version, uint32_t target_version){
	for(uint32_t i=start_lba; i<=end_lba; i++){
		if(original_version==UINT32_MAX || version_map_lba(v, i)==original_version){
			if(i==debug_lba){
				EPRINT("target lba's version is updated",false);
			}
			version_coupling_lba_ridx(v, i, target_version);
		}
		else{
			if(i==debug_lba){
				/*
				printf("\nversion info:%u, ", version_map_lba(v, i));
				EPRINT("target lba's version is [not] updated",false);*/
			}
		}
	}
	return 1;
}

void version_make_early_invalidation_enable_old(version *v){
	for(uint32_t i=0; i<v->max_valid_version_num-1; i++){
		v->version_early_invalidate[version_to_run(v,i)]=true;
	}
}


uint32_t version_level_to_start_version(level *lev){
	uint32_t above_run=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		if(LSM.disk[i]==lev){
			return above_run+1;
		}
		else{
			above_run+=(i==LSM.param.LEVELN-1?LSM.param.last_size_factor:LSM.param.normal_size_factor);
		}
	}
	return above_run;
}
