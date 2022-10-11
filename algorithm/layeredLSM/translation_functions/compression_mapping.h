#ifndef COMPRESSION_MAP_FUNCTION
#define COMPRESSION_MAP_FUNCTION
#include "../mapping_function.h"
#include "../../../include/data_struct/lru_list.h"

typedef struct compressed_form{
	uint32_t data_size; //bit
	uint32_t max_entry_num;
	uint8_t *data;
	uint32_t bit_num;
}compressed_form;

typedef struct compression_ent{
	void *list_entry;
	compressed_form *comp_data;
	uint64_t mem_bit;
}compression_ent;



map_function* compression_init(uint32_t contents_num, float fpr, uint64_t total_bit);
uint32_t compression_insert(map_function *m, uint32_t lba, uint32_t offset);
uint32_t compression_query(map_function *m, uint32_t lba, map_read_param ** param);
uint32_t compression_retry(map_function *m, map_read_param *param);
uint32_t compression_oob_check(map_function *m, map_read_param *param);
uint64_t compression_get_memory_usage(map_function *m, uint32_t target_bit);
void compression_make_done(map_function *m);
void compression_free(map_function *m);
#endif
