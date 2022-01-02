#ifndef EX_MAP_FUNCTION
#define EX_MAP_FUNCTION
#include "../mapping_function.h"

typedef struct exact_map{
	uint32_t *map;
}exact_map;


map_function* exact_map_init(uint32_t contents_num, float fpr);
uint32_t exact_insert(map_function *m, uint32_t lba, uint32_t offset);
uint32_t exact_query(map_function *m, uint32_t lba, map_read_param ** param);
uint32_t exact_query_retry(map_function *m, map_read_param *param);
uint64_t exact_get_memory_usage(map_function *m, uint32_t target_bit);
void exact_make_done(map_function *m);
void exact_free(map_function *m);
#endif
