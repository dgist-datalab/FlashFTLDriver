#include "../../include/container.h"
#include "../../interface/interface.h"

typedef struct align_gc_buffer{
	uint8_t idx;
	char value[PAGESIZE];
	KEYT key[L2PGAP];
} align_gc_buffer;

typedef struct gc_value{
	uint32_t ppa;
	value_set *value;
	bool isdone;
} gc_value;

void pftl_gc();
void *pftl_gc_end_req(algo_req *input);
