#include "../../include/settings.h"
#include "../../include/container.h"
typedef struct page_params{
	request *parents;
	uint32_t address;
	value_set *value;
}page_params;

typedef struct align_buffer{
	uint8_t idx;
	value_set *value[L2PGAP];
	KEYT key[L2PGAP];
}align_buffer;

uint32_t page_create (lower_info*,blockmanager *, algorithm *);
void page_destroy (lower_info*,  algorithm *);
uint32_t page_argument(int argc, char **argv);
uint32_t page_read(request *const);
uint32_t page_write(request *const);
uint32_t page_remove(request *const);
uint32_t page_flush(request *const);
void *page_end_req(algo_req*);
