#ifndef MAPPING_FUNCTION
#define MAPPING_FUNCTION
#include "../../include/settings.h"
#include "../../include/container.h"
#include "./summary_page.h"
#include <stdint.h>
#define NOT_FOUND UINT32_MAX
#define INSERT_SUCCESS UINT32_MAX
enum{
	EXACT, BF, GUARD_BF, PLR_MAP, TREE_MAP, EMPTY_MAP,
};

enum{
	NOT_RETRY, NORMAL_RETRY, FORCE_RETRY,
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
	uint32_t ste_num;
	struct map_function *mf;
	uint32_t lba;
	uint32_t retry_flag;
	uint8_t intra_offset;
	uint32_t prev_offset;
	uint32_t *oob_set;
	void *private_data;
} map_read_param;

typedef struct {
	uint32_t map_type;
	uint32_t lba_bit;
	float fpr; 
}map_param;

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
 * lba:
 * param: it is assigned by this function to process read
 * */
	uint32_t (*query)(struct map_function *m, uint32_t lba, map_read_param **param_assigned);
/*
 * Function: map_query_by_req
 * --------------------- 
 *		return psa which has probability of storing the target data.
 * 
 * m: 
 * lba:
 * param: it is assigned by this function to process read
 * */
	uint32_t (*query_by_req)(struct map_function *m, request *req, map_read_param **param_assigned);
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
	/*
		Function:map_get_memory_usage
		-----------------------------
			return the memory usage bit of map_function 
		m:
		target_bit: the lba bit of ftl
	*/
	uint64_t (*get_memory_usage) (struct map_function *m, uint32_t target_bit);
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
	bool (*iter_move)(map_iter*);
	void (*iter_adjust)(map_iter *, uint32_t lba);
	void (*iter_free)(map_iter*);


	void *private_data;

	bool moved;
	uint32_t type;
	uint32_t lba_bit_num;
	uint32_t now_contents_num;
	uint32_t max_contents_num;
	uint32_t memory_usage_bit;
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
map_function *map_function_factory(map_param param, uint32_t contents_num);

map_function *map_empty_copy(uint64_t memory_usage_bit);

uint64_t map_memory_per_ent(uint32_t type, uint32_t target_bit, float fpr);

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
	if(param->oob_set[param->intra_offset]==param->lba){
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

static inline char *map_type_to_string(uint32_t map_type){
	switch(map_type){
		case EXACT:
		return "EXACT"; break;
		case BF:
		return "BF"; break;
		case GUARD_BF:
		return "GUARD_BF"; break;
		case PLR_MAP:
		return "PLR_MAP"; break;
		case TREE_MAP:
		return "TREE_MAP"; break;
	}
	return NULL;
}

#endif
