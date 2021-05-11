#ifndef __LEVEL_H__
#define __LEVEL_H__
#include "run.h"
#include "page_manager.h"

enum {LEVELING, LEVELING_WISCKYE, TIERING};


typedef struct level{
	uint32_t idx;
	uint32_t now_sst_num;
	uint32_t max_sst_num;
	uint32_t run_num;
	uint32_t max_run_num;
	//bool istier;
	uint32_t level_type;
	run *array;
}level;

#define LAST_RUN_PTR(lev_ptr) (&(lev_ptr)->array[(lev_ptr)->run_num-1])
#define FIRST_RUN_PTR(lev_ptr) (&(lev_ptr)->array[0])
#define LEVELING_SST_AT_PTR(lev_ptr, idx) &(((lev_ptr)->array[0]).sst_set[idx]) 
#define LEVELING_SST_AT(lev_ptr, idx) (((lev_ptr)->array[0]).sst_set[idx]) 
#define GET_SST_IDX(lev, sptr) ((sptr)-(lev)->array[0].sst_set)
#define GET_LEV_START_LBA(lev) ((lev)->array[0].start_lba)
#define GET_LEV_END_LBA(lev) ((lev)->array[0].end_lba)

#define for_each_run(level_ptr, run_ptr, idx)\
	for(idx=0, (run_ptr)=&(level_ptr)->array[0]; idx<(level_ptr)->run_num; \
			idx++, (run_ptr)=&(level_ptr)->array[idx])

#define for_each_run_max(level_ptr, run_ptr, idx)\
	for(idx=0, (run_ptr)=&(level_ptr)->array[0]; idx<(level_ptr)->max_run_num; \
			idx++, (run_ptr)=&(level_ptr)->array[idx])

#define for_each_sst_level(level_ptr, rptr, ridx, sptr, sidx)\
	for_each_run(level_ptr, rptr, ridx)\
		for_each_sst(rptr, sptr, sidx)

level *level_init(uint32_t max_sst_num, uint32_t run_num, uint32_t level_type, uint32_t idx);
uint32_t level_append_sstfile(level *, sst_file *sptr);
uint32_t level_deep_append_sstfile(level *, run *);
uint32_t level_append_run_copy_move_originality(level *, run *, uint32_t ridx);
uint32_t level_deep_append_run(level *, run *);
uint32_t level_update_run_at_move_originality(level *, uint32_t idx, run *r, bool new_run);
void level_run_reinit(level *lev, uint32_t idx);
static inline void level_shallow_copy_move_originality(level *des, level *src){
	run *temp_array=des->array;
	*des=*src;
	run *rptr;
	uint32_t ridx;
	for_each_run(src, rptr, ridx){
		run_shallow_copy_move_originality(&temp_array[ridx], rptr);
	}
	des->array=temp_array;
}

static inline void level_shallow_copy(level *des, level *src){
	run *temp_array=des->array;
	*des=*src;
	run *rptr;
	uint32_t ridx;
	for_each_run(src, rptr, ridx){
		run_shallow_copy(&temp_array[ridx], rptr);
	}
	des->array=temp_array;
}

void level_trivial_move_sstfile(level *src, level *des,  uint32_t from, uint32_t to);
void level_trivial_move_run(level *, uint32_t from, uint32_t to);
sst_file* level_retrieve_sst(level *, uint32_t lba);
sst_file* level_retrieve_sst_with_check(level *, uint32_t lba);
sst_file* level_retrieve_close_sst(level *, uint32_t lba);
void level_free(level *, page_manager *);
level *level_convert_run_to_lev(run *r, page_manager *pm);
sst_file* level_find_target_run_idx(level *lev, uint32_t lba, uint32_t ppa, uint32_t *version, uint32_t *sptr_idx);
void level_sptr_update_in_gc(level *lev, uint32_t ridx, uint32_t sptr_idx, sst_file *sptr);
void level_sptr_add_at_in_gc(level *lev, uint32_t ridx, uint32_t sptr_idx, sst_file *sptr);
void level_sptr_remove_at_in_gc(level *lev, uint32_t ridx, uint32_t sptr_idx);

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
	return true;
}

static inline bool level_is_full(level *lev){
	if(lev->istier){
		return !(lev->run_num<lev->max_run_num);
	}
	else{
		return !(lev->now_sst_num<lev->max_sst_num);
	}
}

static inline bool level_is_appendable(level *lev, uint32_t append_target_num){
	return lev->now_sst_num+append_target_num <= lev->max_sst_num;
}

void level_print(level *lev);
void level_content_print(level *lev, bool print_sst);

#endif
