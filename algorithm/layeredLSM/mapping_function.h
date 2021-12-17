#ifndef MAPPING_FUNCTION
#define MAPPING_FUNCTION
#include "../../include/settings.h"
#include "../../include/container.h"
#include "./summary_page.h"
#include <stdint.h>
#define NOT_FOUND UINT32_MAX
#define INSERT_SUCCESS UINT32_MAX
enum{
	EXACT, BF, GUARD_BF, PLR_MAP, TREE_MAP
};
typedef struct map_iter{
	uint32_t read_pointer;
	bool iter_done_flag;
	struct map_function *m;
	void *private_data;
}map_iter;

typedef struct map_read_param{
	struct request *p_req;
	struct run *r;
	struct map_function *mf;
	bool retry_flag;
	uint8_t intra_offset;
	uint32_t prev_offset;
	uint32_t *oob_set;
	void *private_data;
} map_read_param;

typedef struct map_function{
/*
 * Function: map_insert
 * --------------------- 
 *		inserting lba and offset
 *		return old ppa if the inserted lba already is in the map, when the type of map is exact type
 *		otherwise return INSERT_SUCCESS
 *
 * m:
 * lba: target lba
 * offset: target offset (which is psa)
 *
 * */
	uint32_t (*insert)(struct map_function *m, uint32_t lba, uint32_t offset);

/*
 * Function: map_query
 * --------------------- 
 *		return psa which has probability of storing the target data.
 * 
 * m: 
 * request: target request for quering
 * param: it is assigned by this function to process read
 * */
	uint32_t (*query)(struct map_function *m, request *req, map_read_param **param_assigned);
	uint32_t (*oob_check)(struct map_function *m, map_read_param *param);
/*
 * Function: map_query_retry
 * ---------------------------
 *		return the same as the exact_query
 *		since the fpr is always 0, the retry logic is useless
 *
 *	m:
 *	param: previous query information
 *
 * */
	uint32_t (*query_retry)(struct map_function *m, map_read_param *param);
	void (*query_done)(struct map_function *m, map_read_param *param);
/*
 * Function: bf_map_make_done
 * ------------------------
 *		this function is called when the inserting data into mapping_function is finished
 * 
 * m:
 *
 * */
	void (*make_done)(struct map_function *m);

/*
 * Function: map_make_summary
 * -------------------------
 *		insert sorted mapping information into data by lba
 *
 * data:
 * start_lba: it is set to start lba of summary_page
 * start: if it is first call of mapping_function
 * 
	void (*make_summary)(struct map_function *m, char *data, uint32_t *start_lba, bool first);
	*/
	void (*show_info)(struct map_function *m);
/*
 * Function: bf_map_free
 * --------------------
 *		free allocated exact_map
 *
 *	m:
 *
 * */
	void (*free)(struct map_function *m);

	map_iter *(*iter_init)(struct map_function *m);
	summary_pair (*iter_pick)(map_iter *);
	void (*iter_move)(map_iter*);
	void (*iter_free)(map_iter*);


	void *private_data;

	uint32_t type;
	uint32_t lba_bit_num;
	uint32_t now_contents_num;
	uint32_t max_contents_num;
	//uint32_t make_summary_lba;
}map_function;

/*
	Function: map_function_factory
	------------------------------
		return map_function for run by passed type
	
	type: type of map_function (EXACT, BF, GUARD_BF, PLR)
	contents_num: the max number of entries in the map_function
	fpr: the target error rate for map_function
 */
map_function *map_function_factory(uint32_t type, uint32_t contents_num, float fpr, uint32_t lba_bit_num);

/*
 * Function: map_default_oob_check
 * --------------------- 
 *		return offset of target lba in physical
 *		if not found the lba in oob, return NOT_FOUND
 * 
 * m: 
 * param: target read param to check
 * */
static inline uint32_t map_default_oob_check(map_function *m, map_read_param *param){
	if(param->oob_set[param->intra_offset]==param->p_req->key){
		return param->intra_offset;
	}
	return NOT_FOUND;
}

/*
 * Function: map_default_query_done
 * --------------------- 
 *		free allocated param
 * 
 * mf: 
 * param: target param
 * */
static inline void map_default_query_done(map_function *mf, map_read_param *param){
	free(param);
}

static inline void map_init(map_function *mf,uint32_t type,
		uint32_t max_contents_num, uint32_t lba_bit_num){
	mf->type=type;
	mf->max_contents_num=max_contents_num;
	mf->now_contents_num=0;
	mf->lba_bit_num=lba_bit_num;
}

static inline bool map_full_check(map_function *mf){
	return mf->now_contents_num > mf->max_contents_num;
}

static inline void map_increase_contents_num(map_function *mf){
	mf->now_contents_num++;
}

#endif
