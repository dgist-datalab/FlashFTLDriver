#ifndef PLR_MAP_FUNCTION
#define PLR_MAP_FUNCTION

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "./plr/plr.h"
#include "../mapping_function.h"
#include "../../../include/debug_utils.h"

#define SLOPE_BIT 7

#define extract_plr(mf) ((plr_map*)mf->private_data)

class PLR;

typedef struct plr_map{
	PLR *plr_body;
}plr_map;

/*
 * Function: plr_map_init
 * --------------------
 *		return plr_map
 *
 * contents_num: the max number of contents_num in exact_map
 * fpr: target fpr
 * */
map_function *plr_map_init(uint32_t contents_num, float fpr);
uint32_t plr_map_insert(map_function *mf, uint32_t lba, uint32_t offset);
uint32_t plr_oob_check(map_function *mf, map_read_param *param);
uint32_t plr_map_query(map_function *mf, uint32_t lba, map_read_param **param);
uint32_t plr_map_query_retry(map_function *mf, map_read_param *param);
void plr_map_make_done(map_function *mf);
uint64_t plr_get_memory_usage(map_function *mf, uint32_t target_bit);
void plr_map_free(map_function *mf);
#endif
