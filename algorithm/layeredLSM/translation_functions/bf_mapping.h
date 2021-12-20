#ifndef BF_MAP_FUNCTION
#define BF_MAP_FUNCTION

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bf.h"
#include "../mapping_function.h"
#include "../../../include/debug_utils.h"

#define extract_map(a, b)\
	bf_map *a=(bf_map*)(b)->private_data

typedef struct bloom_filter_map{
	bloom_filter_meta *bfm;
	bloom_filter *set_of_bf;
	uint32_t write_pointer;
}bf_map;

/*
 * Function: bf_map_init
 * --------------------
 *		return bf_mapping
 *
 * contents_num: the max number of contents_num in exact_map
 * fpr: target fpr
 * */
map_function *bf_map_init(uint32_t contents_num, float fpr);
uint32_t bf_map_insert(map_function *mf, uint32_t lba, uint32_t offset);
uint32_t bf_map_query(map_function *mf, uint32_t lba, map_read_param **param);
uint32_t bf_map_query_retry(map_function *mf, map_read_param *param);
void bf_map_make_done(map_function *mf);
void bf_map_free(map_function *mf);

#endif
