#ifndef __VERSION_H__
#define __VERSION_H__
#include <stdlib.h>
#include <stdint.h>
#include <queue>
#include "../../include/settings.h"
#include "lsmtree.h"
#define TOTALRUNIDX 31

typedef struct version{
	uint8_t start_hand;
	uint8_t end_hand;
	uint8_t *key_version;//key->ridx
	int8_t valid_version_num;
	int8_t max_valid_version_num;
	std::queue<uint32_t> *ridx_empty_queue;
	std::queue<uint32_t> *ridx_populate_queue;
	uint32_t memory_usage_bit;
	int32_t poped_version_num;
}version;

version *version_init(uint8_t max_valid_version_num, uint32_t LBA_num);
uint32_t version_get_empty_ridx(version *v);
void version_get_merge_target(version *v, uint32_t *ridx_set);
void version_unpopulate_run(version *v, uint32_t ridx);
void version_populate_run(version *v, uint32_t ridx);
void version_sanity_checker(version *v);
void version_free(version *v);
void version_coupling_lba_ridx(version *v, uint32_t lba, uint8_t ridx);

static inline uint32_t version_map_lba(version *v, uint32_t lba){
	return v->key_version[lba];
}

static inline int version_compare(version *v, int32_t a, int32_t b){
	//a: recent version
	//b: noew version
	if(b > v->max_valid_version_num){
		EPRINT("not valid comparing", true);
	}
	int a_=a-v->poped_version_num<0?a-v->poped_version_num+v->max_valid_version_num:a;
	int b_=b-v->poped_version_num<0?b-v->poped_version_num+v->max_valid_version_num:b;
	return a_-b_;
}
static inline void version_poped_update(version *v){
	v->poped_version_num+=2;
	v->poped_version_num%=v->max_valid_version_num;
}
static inline uint32_t version_level_idx_to_version(version *v, uint32_t lev_idx, uint32_t level_num){
	return level_num-1-lev_idx+v->max_valid_version_num-1;
}
#endif
