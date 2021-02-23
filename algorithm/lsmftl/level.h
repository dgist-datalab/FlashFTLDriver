#ifndef __LEVEL_H__
#define __LEVEL_H__
#include "run.h"

enum {LEVELING, TIERING};


typedef struct level{
	uint32_t idx;
	uint32_t now_sst_num;
	uint32_t max_sst_num;
	uint32_t run_num;
	uint32_t max_run_num;
	bool istier;
	run *array;
}level;

#define LAST_RUN_PTR(lev_ptr) (&(lev_ptr)->array[(lev_ptr)->run_num-1])
#define FIRST_RUN_PTR(lev_ptr) (&(lev_ptr)->array[0])
#define LEVELING_SST_AT_PTR(lev_ptr, idx) &(((lev_ptr)->array[0]).sst_set[idx]) 

level *level_init(uint32_t max_sst_num, uint32_t run_num, bool istier, uint32_t idx);
run *level_find_run(level *, uint32_t lba);
uint32_t level_append_sstfile(level *, sst_file *sptr);
uint32_t level_deep_append_sstfile(level *, run *);
void level_trivial_move_sstfile(level *src, level *des,  uint32_t from, uint32_t to);
void level_trivial_move_run(level *, uint32_t from, uint32_t to);
void level_reinit(level *);
void level_free(level *);

static inline bool level_check_overlap(level *a, level *b){
	if(b->now_sst_num==0 || a->now_sst_num==0) return false;
	if(FIRST_RUN_PTR(a)->start_lba > LAST_RUN_PTR(b)->end_lba || 
			LAST_RUN_PTR(a)->end_lba < FIRST_RUN_PTR(b)->start_lba)
		return false;
	return true;
}

static inline bool level_check_overlap_keyrange(uint32_t start, uint32_t end, level *lev){
	if(lev->now_sst_num==0) return false;
	if(LAST_RUN_PTR(lev)->start_lba > end || LAST_RUN_PTR(lev)->end_lba < start){
		return false;
	}
	return false;
}

static inline bool level_is_full(level *lev){
	return !(lev->now_sst_num < lev->max_sst_num);
}

static inline bool level_is_appendable(level *lev, uint32_t append_target_num){
	return lev->now_sst_num+append_target_num < lev->max_sst_num;
}

#define for_each_run(level_ptr, run_ptr, idx)\
	for(idx=0, (run_ptr)=&(level_ptr)->array[0]; idx<(level_ptr)->run_num; \
			idx++, (run_ptr)=&(level_ptr)->array[idx])

#define for_each_sst_level(level_ptr, rptr, ridx, sptr, sidx)\
	for_each_run(level_ptr, rptr, ridx)\
		for_each_sst(rptr, sptr, sidx)

#endif
