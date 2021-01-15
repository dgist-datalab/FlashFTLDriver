#include "../../include/container.h"
#include "../../interface/interface.h"

typedef struct pftl_map_body{
	uint32_t *mapping;
	uint32_t assign_page;
	bool isfull;

	__segment *reserve;
	__segment *active;
} pm_body;

void pftl_mapping_create();
void pftl_mapping_free();
uint32_t pftl_assign_ppa(KEYT *lba);
uint32_t pftl_get_mapped_ppa(uint32_t lba);
uint32_t pftl_map_trim(uint32_t lba);
uint32_t pftl_gc_update_mapping(KEYT *lba, uint32_t idx);
