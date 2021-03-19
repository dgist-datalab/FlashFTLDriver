#ifndef __GC_H__
#define __GC_H__
#include "../../include/container.h"
#include "../../interface/interface.h"
#include "page_manager.h"
#include "../../include/sem_lock.h"

typedef struct gc_read_node{
	bool is_mapping;
	value_set *data;
	fdriver_lock_t done_lock;
	uint32_t piece_ppa;
	uint32_t lba;
}gc_read_node;

void do_gc(page_manager *pm);

#endif
