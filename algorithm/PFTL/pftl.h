#include "../../include/settings.h"
#include "../../include/container.h"

typedef struct pftl_params{
	request *parents;
	value_set *value;
} pftl_params;

typedef struct align_buffer{
	uint8_t idx;
	value_set *value[L2PGAP];
	KEYT key[L2PGAP];
} align_buffer;

uint32_t pftl_create(lower_info *, blockmanager *, algorithm *);
void pftl_destroy(lower_info *, algorithm *);
uint32_t pftl_read(request *const);
uint32_t pftl_write(request *const);
uint32_t pftl_remove(request *const);
uint32_t pftl_flush(request *const);
void *pftl_end_req(algo_req *);
