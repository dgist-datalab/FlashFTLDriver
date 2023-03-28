#include "../../include/container.h"
#include "../../interface/interface.h"
#include "../../include/data_struct/redblack.h"
#include "../../interface/queue.h"


#ifndef __grouping__
#define __grouping__
#define GNUMBER 6
#define BENCH_JY 1
//0 is zip08, 1 is zip11, 2 is random
#endif

typedef struct page_stat {
	unsigned long user_write;
	unsigned long gc_write;
	uint32_t gsize[6];
	double vr[6];
	double erase[6];
} STAT;


typedef struct page_map_body{
	uint32_t *mapping;
	bool isfull;
	uint32_t assign_page;

	unsigned long *seg_timestamp;
	unsigned long avg_segage;
	unsigned long tmp_avg;
	int gc_seg;
	Redblack rb_lbas;
	queue *q_lbas;
	STAT* stat;

	/*segment is a kind of Physical Block*/
	__segment **reserve; //for gc
	__segment **active; //for gc
}pm_body;

int is_lba_hot(uint32_t lba);
void q_size_down(int nsize);
void page_map_create();
void print_stat();
uint32_t page_map_assign(KEYT *lba, uint32_t max_idx, int hc_cnt, unsigned long* timestamp);
uint32_t page_map_pick(uint32_t lba);
uint32_t page_map_trim(uint32_t lba);
uint32_t page_map_gc_update(KEYT* lba, uint32_t idx, uint32_t mig_count);
void page_map_free();
