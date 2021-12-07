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

/*
 * Function: bf_map_insert
 * --------------------- 
 *		inserting lba and offset
 *
 * m:
 * lba: target lba
 * offset: target offset (which is psa)
 *
 * */
void bf_map_insert(map_function *mf, uint32_t lba, uint32_t offset);

/*
 * Function: bf_map_query
 * --------------------- 
 *		return psa which has probability of storing the target data.
 * 
 * m: 
 * request: target request for quering
 * param: it is assigned by this function to process read
 * */
uint32_t bf_map_query(map_function *mf, request *req, map_read_param **param);

/*
 * Function: bf_query_retry
 * ---------------------------
 *		return the same as the exact_query
 *		since the fpr is always 0, the retry logic is useless
 *
 *	m:
 *	param: previous query information
 *
 * */
uint32_t bf_map_query_retry(map_function *mf, map_read_param *param);

/*
 * Function: bf_map_make_done
 * ------------------------
 *		this function is called when the inserting data into mapping_function is finished
 * 
 * m:
 *
 * */
void bf_map_make_done(map_function *mf);

/*
 * Function: bf_map_free
 * --------------------
 *		free allocated exact_map
 *
 *	m:
 *
 * */
void bf_map_free(map_function *mf);

#endif
