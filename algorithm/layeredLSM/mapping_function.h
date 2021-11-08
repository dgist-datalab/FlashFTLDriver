#ifndef MAPPING_FUNCTION
#define MAPPING_FUNCTION
#include "../../include/settings.h"
#include <stdint.h>
#define NOT_FOUND UINT32_MAX
enum{
	EXACT, BF, GUARD_BF, PLR
};

typedef struct mapping_function{
	void (*insert)(struct mapping_function *m, uint32_t lba, uint32_t offset);
	uint32_t (*query)(struct mapping_function *m, uint32_t lba);
	uint32_t (*query_retry)(struct mapping_function *m, uint32_t lba,
			uint32_t prev_offset, uint32_t *oob_set);
	void (*make_done)(struct mapping_function *m);
	void (*show_info)(struct mapping_function *m);
	void (*free)(struct mapping_function *m);
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

#endif
