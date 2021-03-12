#ifndef __PAGE_MANAGER_H__
#define __PAGE_MANAGER_H__
#include "../../include/settings.h"
#include "../../include/container.h"
#include <stdint.h>

#define MAX_MAP (PAGESIZE/sizeof(uint32_t))
#define BLOCK_MAP_SIZE (_PPB*sizeof(uint32_t))
#define BLOCK_PER_MAP_NUM (BLOCK_MAP_SIZE/PAGESIZE+(BLOCK_MAP_SIZE%PAGESIZE?1:0))
#define PIECETOPPA(a) ((a)>>1)


typedef struct _page_manager{
	bool seg_type_checker[_NOS];
	bool is_master_page_manager;
	__segment *current_segment[PARTNUM];
	__segment *reserve_segment;
	struct blockmanager *bm;
}page_manager;

page_manager* page_manager_init(struct blockmanager *bm);
void page_manager_free(page_manager* pm);
bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page);
bool page_manager_oob_lba_checker(page_manager *pm, uint32_t piece_ppa, uint32_t lba, uint32_t *idx);
uint32_t page_manager_get_new_ppa(page_manager *pm, bool ismap);
uint32_t page_manager_pick_new_ppa(page_manager *pm, bool ismap);
uint32_t page_manager_get_remain_page(page_manager *pm, bool ismap);
void validate_piece_ppa(blockmanager *bm, uint32_t piece_num, uint32_t* piece_ppa, uint32_t *lba);
void invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa);

#endif
