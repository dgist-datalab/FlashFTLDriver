#include "../../include/container.h"
#include "../../interface/interface.h"
#include "demand_mapping.h"

typedef struct align_gc_buffer{
	uint8_t idx;
	char value[PAGESIZE];
	KEYT key[L2PGAP];
}align_gc_buffer;

typedef struct gc_value{
	uint32_t ppa;
	value_set *value;
	bool isdone;
}gc_value;

typedef struct pm_body{
	__segment *active;
	__segment *reserve;

	__segment *map_active;
	__segment *map_reserve;
}pm_body;

void invalidate_ppa(uint32_t t_ppa);
void validate_ppa(uint32_t t_ppa, KEYT *lbas);
ppa_t get_ppa(KEYT* lba);
ppa_t get_rppa(KEYT *, uint8_t num, mapping_entry *, uint32_t *idx);
ppa_t get_map_ppa(KEYT gtd_idx);
ppa_t get_map_rppa(KEYT gtd_idx);
void do_gc();
void do_map_gc();

void *page_gc_end_req(algo_req *input);
gc_value* send_req(uint32_t ppa, uint8_t type, value_set *value, gc_value *gv);
