#ifndef __VERSION_H__
#define __VERSION_H__
#include <stdlib.h>
#include <stdint.h>
#include <queue>

typedef struct version{
	uint8_t start_hand;
	uint8_t end_hand;
	uint8_t *key_version;//key->ridx
	int8_t valid_version_num;
	int8_t max_valid_version_num;
	std::queue<uint32_t> *ridx_empty_queue;
	std::queue<uint32_t> *ridx_populate_queue;
	uint32_t memory_usage_bit;
}version;

version *version_init(uint8_t max_valid_version_num, uint32_t LBA_num);
uint32_t version_get_empty_ridx(version *v);
void version_get_merge_target(version *v, uint32_t *ridx_set);
void version_unpopulate_run(version *v, uint32_t ridx);
void version_populate_run(version *v, uint32_t ridx);
void version_sanity_checker(version *v);
void version_free(version *v);

static inline void version_coupling_lba_ridx(version *v, uint32_t lba, uint8_t ridx){
	v->key_version[lba]=ridx;
}

static inline uint32_t version_map_lba(version *v, uint32_t lba){
	return v->key_version[lba];
}


#endif
