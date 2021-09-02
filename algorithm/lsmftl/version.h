#ifndef __VERSION_H__
#define __VERSION_H__
#include <stdlib.h>
#include <stdint.h>
#include <queue>
#include "compaction.h"
#include "../../include/settings.h"
#include "../../include/sem_lock.h"
#include "lsmtree.h"
//#define TOTALRUNIDX 31
#define MERGED_RUN_NUM 2
#define INVALID_MAP UINT32_MAX
#define QUEUE_TYPE_NUM 2

enum{
	VERSION, RUNIDX,
};

typedef struct version{
	uint8_t start_hand;
	uint8_t end_hand;
	uint8_t *key_version;//key->ridx
	int8_t valid_version_num;
	int8_t total_version_number;
	int8_t max_valid_version_num;
	uint32_t *start_vidx_of_level;
	fdriver_lock_t version_lock;
	std::list<uint32_t> ***version_empty_queue;
	std::list<uint32_t> ***version_populate_queue;

	/*for mapping version-order-runidx*/
	uint32_t **V2O_map;
	uint32_t **O2V_map;
	uint32_t **R2O_map;
	uint32_t **O2R_map;
	uint32_t *level_order_token;

	uint32_t *version_invalidate_number;
	uint32_t memory_usage_bit;
	uint32_t *level_run_num;
	uint32_t *poped_version_num;
	uint32_t leveln;
}version;

version *version_init(uint8_t total_version_number, uint32_t LBA_num, level **disk, uint32_t leveln);
uint32_t version_get_empty_version(version *v, uint32_t level);

static inline uint32_t version_pop_oldest_version(version *v, uint32_t level){
	uint32_t res=v->version_populate_queue[level][VERSION]->front();
	v->version_populate_queue[level][VERSION]->pop_front();
	v->version_populate_queue[level][RUNIDX]->pop_front();
	return res;
}

static inline uint32_t version_pick_oldest_version(version *v, uint32_t level){
	return v->version_populate_queue[level][VERSION]->front();
}

void version_get_merge_target(version *v, uint32_t *version_set, uint32_t level_idx);
void version_clear_merge_target(version *v, uint32_t *version_set, uint32_t level_idx);
void version_repopulate_merge_target(version *v, uint32_t version,
		uint32_t ridx, uint32_t level_idx);
void version_update_mapping_merge(version *v, uint32_t *version_set, uint32_t level_idx);

void version_clear_level(version *v, uint32_t level_idx);

void version_populate(version *v, uint32_t version, uint32_t level_idx);
void version_sanity_checker(version *v);
void version_free(version *v);
void version_coupling_lba_version(version *v, uint32_t lba, uint8_t version);
uint32_t version_update_for_trivial_move(version *v, uint32_t start_lba, uint32_t end_lba, 
		uint32_t src_level_idx, uint32_t des_level_idx, uint32_t target_version);
uint32_t version_level_to_start_version(version *v, uint32_t level_idx);

uint32_t version_get_level_invalidation_cnt(version *v, uint32_t level_idx);
uint32_t version_get_resort_version(version *v, uint32_t level_idx);
uint32_t version_decrease_invalidation_number(version *v, uint32_t _version, 
		int32_t decrease_num);

void version_reunpopulate(version *v, uint32_t version, uint32_t level_idx);
void version_clear_target(version *v, uint32_t version, uint32_t level_idx);

static inline uint32_t version_order_to_ridx(version *v, uint32_t lev_idx, uint32_t order){
	return v->O2R_map[lev_idx][order];
}

static inline uint32_t version_ridx_to_order(version *v, uint32_t lev_idx, uint32_t _ridx){
	return v->R2O_map[lev_idx][_ridx];
}

static inline uint32_t version_order_to_version(version *v, uint32_t lev_idx, uint32_t order){
	uint32_t start_version=version_level_to_start_version(v, lev_idx);
	return v->O2V_map[lev_idx][order]+start_version;
}

static inline uint32_t version_ridx_to_version(version *v, uint32_t lev_idx, uint32_t ridx){
	return version_order_to_version(v, lev_idx, 
			version_ridx_to_order(v, lev_idx, ridx));
}

static inline uint32_t version_map_lba(version *v, uint32_t lba){
	uint32_t res;
	fdriver_lock(&v->version_lock);
	res=v->key_version[lba];
	fdriver_unlock(&v->version_lock);
	return res;
}

static void version_invalidate_number_init(version *v, uint32_t version){
	v->version_invalidate_number[version]=0;
}

static void version_level_invalidate_number_init(version *v, uint32_t lev_idx){
	uint32_t start_vidx=version_level_to_start_version(v, lev_idx);
	uint32_t end_vidx=lev_idx==0?v->total_version_number-1:version_level_to_start_version(v, lev_idx-1)-1;
	for(uint32_t i=start_vidx; i<=end_vidx; i++){
		version_invalidate_number_init(v, i);
	}
}

void version_print_order(version *v, uint32_t version_number);

static inline bool version_belong_level(version *v, int32_t a, uint32_t lev_idx){
	uint32_t start_v=v->start_vidx_of_level[lev_idx];
	uint32_t end_v=(lev_idx!=0?
			v->start_vidx_of_level[lev_idx-1]-1:
			v->max_valid_version_num-1);
	if(start_v<=a && a<=end_v){
		return true;
	}
	return false;
}

static inline int version_to_belong_level(version *v, uint32_t a){
	uint32_t i=0;
	for(; i<v->leveln; i++){
		if(v->start_vidx_of_level[i] <=a) return i;
	}
	return v->leveln-1;
}

static inline int version_to_order(version *v, uint32_t level_idx, int32_t version){
	version-=v->start_vidx_of_level[level_idx];
	return v->V2O_map[level_idx][version];
}

static inline uint32_t version_to_ridx(version *v, uint32_t lev_idx, uint32_t target_version){
	return version_order_to_ridx(v, lev_idx, 
			version_to_order(v, lev_idx, target_version));
}

static inline int version_compare(version *v, int32_t a, int32_t b){
	//a: recent version
	//b: noew version
	if(b > v->max_valid_version_num){
		if(b==UINT8_MAX){
			return a-b;
		}
		EPRINT("not valid comparing", true);
	}

	if(a > v->max_valid_version_num){
		if(a==UINT8_MAX){
			return a-b;
		}
		EPRINT("not valid comparing", true);
	}
	uint32_t a_level=version_to_belong_level(v,a);
	uint32_t b_level=version_to_belong_level(v,b);
	int a_=version_to_order(v, a_level, a)+version_level_to_start_version(v, a_level);
	int b_=version_to_order(v, b_level, b)+version_level_to_start_version(v, b_level);
	/*
	if(version_belong_level(v, a, v->leveln-1)){ //check 
		a_=a-v->poped_version_num<0?a-v->poped_version_num+v->last_level_version_num:a-v->poped_version_num;
	}
	if(version_belong_level(v, b, v->leveln-1)){
		b_=b-v->poped_version_num<0?b-v->poped_version_num+v->last_level_version_num:b-v->poped_version_num;
	}*/
	return a_-b_;
}
static inline void version_poped_update(version *v, uint32_t leveln, uint32_t updating_run_num){
	v->poped_version_num[leveln]+=updating_run_num;
	v->poped_version_num[leveln]%=v->level_run_num[leveln];
}
static inline uint32_t version_to_level_idx(version *v, uint32_t version, uint32_t level_num){
	for(uint32_t i=0; i<level_num-1; i++){
		if(i==0){
			if(v->start_vidx_of_level[i]<=version) return i;
		}
		else{
			if(v->start_vidx_of_level[i]<=version &&
					version<v->start_vidx_of_level[i-1]){
				return i;	
			}
		}
	}
	return level_num-1;
	/*
	if(version<v->max_valid_version_num){
		return level_num-1;
	}
	else
		return level_num-version+v->max_valid_version_num-2;*/
}
//void version_traversal(version *v);

#define for_each_old_ridx_in_lev(v, ridx, cnt, lev_idx)\
	for(cnt=0, ridx=version_to_ridx(v, lev_idx, version_pick_oldest_version(v, lev_idx));\
			cnt<(v)->level_run_num[lev_idx];\
			cnt++, ridx=(ridx+1)%(v)->level_run_num[lev_idx])

#endif
