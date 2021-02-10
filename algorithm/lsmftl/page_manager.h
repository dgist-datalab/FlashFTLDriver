#ifndef __PAGE_MANAGER_H__
#define __PAGE_MANAGER_H__
#include "../../include/settings.h"
#include "../../include/container.h"
#include <stdint.h>

#define MAX_MAP (PAGESIZE/sizeof(uint32_t))
#define BLOCK_MAP_SIZE (_PPB*sizeof(uint32_t))
#define BLOCK_PER_MAP_NUM (BLOCK_MAP_SIZE/PAGESIZE+(BLOCK_MAP_SIZE%PAGESIZE?1:0))

union _lba_map_wrapper{
	uint32_t data[BLOCK_PER_MAP_NUM][MAX_MAP];
	uint32_t **ptr;
};

typedef struct _page_manager{
	bool is_master_page_manager;
	__segment *current_segment;
	__segment *reserve_segment;
	int current_lba_map_ptr;
	_lba_map_wrapper lba_map;
	struct blockmanager *bm;
}page_manager;

page_manager* page_manager_init(bool is_master, char **map_data, int num_of_map, \
		struct blockmanager *bm);
void page_manager_insert_lba(page_manager *pm, uint32_t lba);
void page_manager_free(page_manager* pm);
bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page);
uint32_t page_manager_get_new_ppa(page_manager *pm);
//void page_manager_align_block(page_manager *pm, uint32_t number_of_data_page);
/*
static inline uint32_t page_manger_lba_at(page_manager *pm, uint32_t idx){
	if(idx >= _PPB*MAX_MAP) return UINT32_MAX;
	return pm->lba_map.data[idx/MAX_MAP][idx%MAX_MAP];
}

#define for_each_pm_lba(pm, idx, lba)\
	for((idx)=0, (lba)=page_manager_lba_at((pm), (idx)); idx<(_PPB*MAX_MAP);\
			(lba)=page_manager_lba_at(++idx))
*/
#endif
