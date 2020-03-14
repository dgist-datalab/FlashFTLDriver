#include "../../include/container.h"
#include "../../interface/interface.h"

typedef struct gc_value{
	uint32_t ppa;
	value_set *value;
	bool isdone;
}gc_value;

void invalidate_ppa(uint32_t ppa);
void validate_ppa(uint32_t ppa, uint32_t lba);
void do_gc();

void *page_gc_end_req(algo_req *input);
