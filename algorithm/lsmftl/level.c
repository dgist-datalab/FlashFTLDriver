#include "level.h"
#include "run.h"

level *level_init(uint32_t max_sst_num){
	level *res=(level*)calloc(1,sizeof(level));
	res->array=(run*)calloc(now_run_num, sizeof(run));
	return res;
}

run *level_find_run(level *lev, uint32_t lba){
	uint32_t s=0,e=lev->now_run_num,mid;
	while(s<e){
		mid=(s+e)/2;
		if(lev->array[mid].start_lba < lba){
			s=mid+1;
		}
		else{
			e=mid;
		}
	}
	return &lev->array[e+1];
}

uint32_t level_append_sstfile(level *lev, sst_file *sptr){
	lev->now_sst_num++;
	return run_append_sstfile(LAST_RUN_PTR(lev), sptr);
}

uint32_t level_deep_append_sstfile(level *lev, sst_file *sptr){
	lev->now_sst_num++;
	return run_deep_append_sstfile(LAST_RUN_PTR(lev), sptr);
}

void level_trivial_move_sstfile(level *des, run *src, uint32_t from, uint32_t to){
	uint32_t start=from;
	uint32_t run_idx=0;
	sst_file *sst_ptr;
	for_each_sst_at(src, sst_ptr, from){
		if(from>to) break;
		des->now_sst_num++;
		run_deep_append_sstfile(LAST_RUN_PTR(des), sst_ptr);
	}
}

void level_trivial_move_run(level *des, level *src, uint32_t from, uint32_t to, uint32_t num){
	if(level_is_full(des) || level_is_appendable(des, src->now_sst_num)){
		EPRINTF("no space to insert run", true);
	}
	run *run_ptr;
	uint32_t idx=0;
	for_each_run(src, run_ptr, idx){
		des->array[des->run_num]=*run_ptr;
		des->run_num++;
	}
}

void level_reinit(level *lev){
	run *run_ptr;
	uint32_t idx;
	for_each_run(lev, run_ptr, idx){
		free_run(run_ptr);
	}
}

void level_free(level *lev){
	level_reinit(lev);
	free(lev->array);
	free(lev);
}
