#ifndef LEVEL_H
#define LEVEL_H
#include <stdio.h>
#include <stdlib.h>
#include <list>
#include "./run.h"
#include "../../include/debug_utils.h"
typedef struct level{
	uint32_t level_idx;
	uint32_t now_run_num;
	uint32_t max_run_num;
	uint32_t map_type;
	std::list<uint32_t> *recency_pointer;
	run **run_array;
}level;

/*
 * Function:level_init
 * ------------------
 *		return level 
 *
 * level_idx: index of level
 * max_run_num: total run_num for each level
 * */
level *level_init(uint32_t level_idx, uint32_t max_run_num, uint32_t map_type);

run *level_get_max_unlinked_run(level *lev);

uint32_t level_pick_max_unlinked_num(level *lev);

/*
 * Function:level_is_full
 * ----------------------
 *		return true when the the number of run in level reaches the max run number
 *
 * lev:
 * */
static inline bool level_is_full(level *lev){
	return lev->now_run_num >=lev->max_run_num;
}

/*
 * Function:level_insert_run
 * -------------------------
 *		insert a run to the level
 *
 *	lev:
 *	r:
 * */
static inline void level_insert_run(level *lev, run *r){
	if(level_is_full(lev)){
		EPRINT("level is full", true);
	}
	for(uint32_t i=0; i<lev->max_run_num; i++){
		uint32_t idx=(lev->now_run_num+i)%lev->max_run_num;
		if(lev->run_array[idx]==NULL){
			lev->run_array[idx]=r;
			lev->recency_pointer->push_front(idx);
			break;
		}
	}
	lev->now_run_num++;
}

static inline uint32_t get_old_run_idx(level *lev, uint32_t th){
	uint32_t idx=0;
	std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
	for(uint32_t i=0; i<th; i++){
		iter++;
	}
	idx=*iter;
	return idx;
}

/*
 * Function:level_get_compaction_target
 * ------------------------------------
 *		return a set of runs which are sorted as some policy to compaction
 *
 * lev:
 * run_num: the number of run in returnning set
 * target: run ptr array for compaction
 * */
void level_get_compaction_target(level *lev, uint32_t run_num, run *** target, bool old_first);

/*
 * Function:level_free
 * -------------------
 *		deallocate level 
 *
 * lev
 * */
void level_free(level* lev);
#endif
