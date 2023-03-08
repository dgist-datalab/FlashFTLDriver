#pragma once
#include "../../include/container.h"
#include "./lea_container.h"
#include <set>

struct temp_map;
typedef struct align_buffer{
	uint8_t idx;
	char **value;
	KEYT key[L2PGAP];
}align_buffer;

typedef struct page_manager{
    __segment *reserve;
    __segment *active;

    lower_info *lower;
    blockmanager *bm;
    std::set<uint32_t> * usedup_segments;
}page_manager;

page_manager *pm_init(lower_info *li, blockmanager *bm);
void pm_page_flush(page_manager *pm, bool isactive, uint32_t type, uint32_t *lba, char **data, uint32_t size, uint32_t *piece_ppa_res);
uint32_t pm_remain_space(page_manager *pm, bool isactive);
__gsegment* pm_get_gc_target(blockmanager *bm);
bool pm_assign_new_seg(page_manager *pm);
void pm_gc(page_manager *pm, __gsegment *target, temp_map *res, bool isdata, bool (*ignore)(uint32_t lba));
void pm_free(page_manager *pm);

void g_buffer_init(align_buffer *);
void g_buffer_free(align_buffer *);
void g_buffer_insert(align_buffer *g_buffer, char *data, uint32_t lba);
void g_buffer_to_temp_map(align_buffer *buf, temp_map *tmap, uint32_t *piece_ppa_res);