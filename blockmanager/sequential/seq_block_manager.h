#ifndef __S_BM_HEADER
#define __S_BM_HEADER
#include "../../include/container.h"
#include "../../interface/queue.h"
#include "../../include/data_struct/heap.h"
#include "../bb_checker.h"
#include "../block_manager_master.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct seq_bm_private{
	__segment seg_set[_NOS];
	mh *max_heap;
	queue *free_segment_q;
	uint32_t num_free_seg;
}sbm_pri;

struct blockmanager *sbm_create(lower_info *li);
void sbm_free(struct blockmanager *bm);
__segment *sbm_get_seg(struct blockmanager *, uint32_t type);
__segment *sbm_pick_seg(struct blockmanager *bm, uint32_t seg_idx, uint32_t type);
int32_t sbm_get_page_addr(__segment *);
int32_t sbm_pick_page_addr(__segment *);
bool sbm_is_gc_needed(struct blockmanager *);
__gsegment* sbm_get_gc_target(struct blockmanager*);
void sbm_trim_segment(struct blockmanager *, __gsegment *);
int sbm_bit_set(struct blockmanager*, uint32_t piece_ppa);
int sbm_bit_unset(struct blockmanager*, uint32_t piece_ppa);
bool sbm_bit_query(struct blockmanager*,uint32_t piece_ppa);
bool sbm_is_invalid_piece(struct blockmanager*,uint32_t piece_ppa);
void sbm_set_oob(struct blockmanager*,char *data, int len, uint32_t ppa);
char *sbm_get_oob(struct blockmanager*,uint32_t ppa);
void sbm_change_reserve_to_active(struct blockmanager *bm, __segment *s);
void sbm_insert_gc_target(struct blockmanager *bm, uint32_t seg_idx);
uint32_t sbm_total_free_page_num(struct blockmanager *bm, __segment *s);
uint32_t sbm_seg_invalidate_piece_num(struct blockmanager *bm, uint32_t seg_idx);
uint32_t sbm_invalidate_seg_num(struct blockmanager *bm);
uint32_t sbm_dump(struct blockmanager *bm, FILE *fp);
uint32_t sbm_load(struct blockmanager *bm, FILE *fp);
#endif
