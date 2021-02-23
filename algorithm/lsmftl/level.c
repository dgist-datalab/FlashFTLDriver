#include "level.h"
#include "run.h"
#include "../../include/settings.h"

level *level_init(uint32_t max_sst_num, uint32_t max_run_num, bool istier, uint32_t idx){
	level *res=(level*)calloc(1,sizeof(level));
	res->idx=idx;
	res->array=(run*)calloc(max_run_num, sizeof(run));
	for(uint32_t i=0; i<max_run_num; i++){
		run_space_init(&res->array[i], max_sst_num/max_run_num, -1, 0);
	}
	res->istier=istier;
	if(!istier){
		res->run_num=1;
	}
	res->now_sst_num=0;
	res->max_sst_num=max_sst_num;
	res->max_run_num=max_run_num;
	return res;
}

run *level_find_run(level *lev, uint32_t lba){
	uint32_t s=0,e=lev->run_num-1,mid;
	while(s<e){
		mid=(s+e)/2;
		if(lev->array[mid].start_lba < lba){
			s=mid+1;
		}
		else{
			e=mid;
		}
	}
	return &lev->array[e];
}

uint32_t level_append_sstfile(level *lev, sst_file *sptr){
	if(lev->now_sst_num >=lev->max_sst_num){
		return 1;
	}
	lev->now_sst_num++;
	run_append_sstfile(LAST_RUN_PTR(lev), sptr);
	return 0;
}

uint32_t level_deep_append_sstfile(level *lev, sst_file *sptr){
	if(lev->now_sst_num >=lev->max_sst_num){
		return 1;
	}
	lev->now_sst_num++;
	run_deep_append_sstfile(LAST_RUN_PTR(lev), sptr);
	return 0;
}

void level_trivial_move_sstfile(level *des, run *src, uint32_t from, uint32_t to){
	sst_file *sst_ptr;
	for_each_sst_at(src, sst_ptr, from){
		if(from>to) break;
		des->now_sst_num++;
		run_deep_append_sstfile(LAST_RUN_PTR(des), sst_ptr);
	}
}

void level_trivial_move_run(level *des, level *src, uint32_t from, uint32_t to, uint32_t num){
	if(level_is_full(des) || level_is_appendable(des, src->now_sst_num)){
		EPRINT("no space to insert run", true);
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
		run_free(run_ptr);
	}
}

void level_free(level *lev){
	level_reinit(lev);
	free(lev->array);
	free(lev);
}
