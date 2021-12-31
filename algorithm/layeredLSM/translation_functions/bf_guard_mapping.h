#ifndef BF_MAP_GUARD_H
#define BF_MAP_GUARD_H

#include "bf.h"
#include "bf_mapping.h"
#include "../mapping_function.h"
#include "../../../include/debug_utils.h"
#define extract_bfg_map(a)\
		((bfg_map*)(a)->private_data)

typedef struct bloom_filter_guard_map{
	bf_map *bf_set;
	uint32_t *guard_set;
	uint32_t write_pointer;
}bfg_map;

map_function *	bfg_map_init(uint32_t contents_num, float fpr, uint32_t bit);
uint32_t			bfg_map_insert(map_function *mf, uint32_t lba, uint32_t offset);
uint32_t		bfg_map_query(map_function *mf, uint32_t lba, map_read_param **param);
uint32_t		bfg_map_query_retry(map_function *mf, map_read_param *param);
void			bfg_map_make_done(map_function *mf);
void			bfg_map_free(map_function *mf);
uint64_t 		bfg_get_memory_usage(map_function *mf, uint32_t target_bit);
float find_sub_member_num(float fpr, uint32_t member, uint32_t lba_bit_num);
#endif
