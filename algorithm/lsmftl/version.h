#ifndef __VERSION_H__
#define __VERSION_H__
#include <stdlib.h>
#include <stdint.h>

typedef struct{
	uint8_t start_hand;
	uint8_t end_hand;
	uint32_t *run_version_list; //version->run_index
	uint8_t *key_version;//key->version
	int8_t valid_version_num;
	int8_t max_valid_version_num;
}version;

version *version_init(uint8_t max_valid_version_num, uint32_t LBA_num);

static inline uint8_t version_get_oldest_run_idx(version *v){
	return v->start_hand;
}

static inline uint8_t version_get_target(version *v, uint32_t lba){
	return v->key_version[lba];
}

static inline void version_insert_key_version(version *v, uint32_t lba, uint8_t version_idx){
	v->key_version[lba]=version_idx;
}

static inline uint8_t version_get_run_index(version *v, uint8_t version_number){	
	return v->run_version_list[version_number];
}

static inline uint8_t version_get_nxt(version *v){
	return v->end_hand;
}

void version_dequeue(version *v);
void version_enqueue(version *v, uint8_t run_idx);
void version_free(version *v);
#endif
