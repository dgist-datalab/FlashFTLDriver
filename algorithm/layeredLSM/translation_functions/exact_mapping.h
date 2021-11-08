#ifndef EX_MAP_FUNCTION
#define EX_MAP_FUNCTION
#include "../mapping_function.h"

typedef struct exact_map{
	uint32_t *map;
	uint32_t max_map_num;
}exact_map;

/*
 * Function: exact_map_init
 * ------------------------
 *		return exact_mapping
 *
 * contents_num: the max number of contents_num in exact_map
 * fpr: it is always 0
 *
 * */
map_function* exact_map_init(uint32_t contents_num, float fpr);

/*
 * Function: exact_insert 
 * --------------------- 
 *		inserting lba and offset
 *
 * m:
 * lba: target lba
 * offset: target offset (which is psa)
 *
 * */
void exact_insert(map_function *m, uint32_t lba, uint32_t offset);

/*
 * Function: exact_query
 * --------------------- 
 *		return physical address (offset) which is retrieved by exact_map
 * 
 * m: 
 * lba: target lba
 * */
uint32_t exact_query(map_function *m, uint32_t lba);

/*
 * Function: exact_query_retry
 * ---------------------------
 *		return the same as the exact_query
 *		since the fpr is always 0, the retry logic is useless
 *
 *	m:
 *	lba: target lba
 *	prev_offset: prev_offset
 *	oob_set: oob_set 
 *
 * */
uint32_t exact_query_retry(map_function *m, uint32_t lba, 
		uint32_t prev_offset, uint32_t *oob_set);

/*
 * Function: exact_make_done
 * ------------------------
 *		this function is called when the inserting data into mapping_function is finished
 * 
 * m:
 *
 * */
void exact_make_done(map_function *m);

/*
 * Function: exact_free
 * --------------------
 *		free allocated exact_map
 *
 *	m:
 *
 * */
void exact_free(map_function *m);
#endif
