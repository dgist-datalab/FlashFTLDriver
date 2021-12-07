#ifndef MAPPING_FUNCTION
#define MAPPING_FUNCTION
#include "../../include/settings.h"
#include "../../include/container.h"
#include <stdint.h>
#define NOT_FOUND UINT32_MAX
enum{
	EXACT, BF, GUARD_BF, PLR
};

typedef struct map_read_param{
	struct request *p_req;
	struct run *r;
	struct map_function *mf;
	uint8_t intra_offset;
	uint32_t prev_offset;
	uint32_t *oob_set;
	void *private_data;
} map_read_param;

typedef struct map_function{
	void (*insert)(struct map_function *m, uint32_t lba, uint32_t offset);
	uint32_t (*query)(struct map_function *m, request *req, map_read_param **param_assigned);
	uint32_t (*oob_check)(struct map_function *m, map_read_param *param);
	uint32_t (*query_retry)(struct map_function *m, map_read_param *param);
	void (*query_done)(struct map_function *m, map_read_param *param);
	void (*make_done)(struct map_function *m);
	void (*show_info)(struct map_function *m);
	void (*free)(struct map_function *m);
	void *private_data;
}map_function;

/*
	Function: map_function_factory
	------------------------------
		return map_function for run by passed type
	
	type: type of map_function (EXACT, BF, GUARD_BF, PLR)
	contents_num: the max number of entries in the map_function
	fpr: the target error rate for map_function
 */
map_function *map_function_factory(uint32_t type, uint32_t contents_num, float fpr);

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

#endif
