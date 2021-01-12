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
	char** ptr;
}

typedef struct _page_manager{
	bool is_master_page_manager;
	__block *current_block;
	__block *reserve_block;
	int current_lba_map_ptr;
	_lba_map_wrapper lba_map;
	struct blockmanager *bm;
}_page_manager;

_page_manager* page_manager_init(bool is_master, char *map_data, struct blockmanager *bm);
void page_manager_insert_lba(_page_manager *pm, uint32_t lba);
void page_manager_free(_page_manager* pm);
uint32_t page_manager_is_gc_needed(_pagem_manager *pm, uint32_t needed_page);
uint32_t page_mangager_get_ppa(_page_manager *pm);

static inline uint32_t page_manger_lba_at(_page_manager *pm, uint32_t idx){
	if(idx >= _PPB*MAX_MAP) return UINT32_MAX;
	return pm->is_master_page_manager?\
		pm->lba_map.data[idx/MAX_MAP][idx%MAX_MAP]:\
		*((uint32_t*)&pm->lba_map.ptr[(idx*sizeof(uint32_t))/PAGESIZE][(idx*sizeof(uint32_t))%PAGESIZE]);
}

#define for_each_pm_lba(pm, idx, lba)\
	for((idx)=0, (lba)=page_manager_lba_at((pm), (idx)); idx<(_PPB*MAX_MAP);\
			(lba)=page_manager_lba_at(++idx))

#endif
