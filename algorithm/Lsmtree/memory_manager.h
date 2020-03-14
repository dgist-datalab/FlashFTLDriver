#ifndef __MM_H_
#define __MM_H_
#include "lsmtree.h"
struct lsmtree;
typedef struct memory_manager{
	int max_memory;
	int usable_memory;
	int lev_least_memory;
	int lev_cache_memory;
	int read_least_memory;
	int read_cache_memory;
}mm_t;

mm_t* mem_init(int um,int lc_min,int r_min);
void mem_free(mm_t*);
bool mem_update(mm_t*,struct lsmtree *lsm,bool);
void mem_trim_lc(mm_t *,struct lsmtree *lsm, uint32_t);
void mem_print();
#endif
