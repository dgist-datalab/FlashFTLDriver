#ifndef GC_H
#define GC_H

#include "../../include/container.h"
#include "../../include/sem_lock.h"
#include "../../interface/interface.h"
#include "./block_table.h"
#include "./sorted_table.h"

#include <stdlib.h>

typedef struct gc_value{
	uint32_t ppa; 
	uint32_t oob[L2PGAP];
	value_set *value;
	fdriver_lock_t lock;
}gc_value;

void gc_summary_segment(L2P_bm *bm, __gsegment *);
void gc_data_segment(L2P_bm *bm, __gsegment *);
void gc(L2P_bm *bm, uint32_t type);

#endif
