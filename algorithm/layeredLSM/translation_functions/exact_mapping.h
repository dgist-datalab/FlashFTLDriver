#ifndef EX_MAP_FUNCTION
#define EX_MAP_FUNCTION
#include "../mapping_function.h"

typedef struct exact_map{
	uint32_t *map;
	uint32_t max_map_num;
	uint32_t now_map_num;
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
 *		return psa which has probability of storing the target data.
 * 
 * m: 
 * request: target request for quering
 * param: it is assigned by this function to process read
 * */
uint32_t exact_query(map_function *m, request *req, map_read_param ** param);

/*
 * Function: exact_query_retry
 * ---------------------------
 *		return the same as the exact_query
 *		since the fpr is always 0, the retry logic is useless
 *
 *	m:
 *	param: previous query information
 *
 * */
uint32_t exact_query_retry(map_function *m, map_read_param *param);

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
