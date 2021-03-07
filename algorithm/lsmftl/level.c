#include "level.h"
#include "run.h"
#include "../../include/settings.h"

level *level_init(uint32_t max_sst_num, uint32_t max_run_num, bool istier, uint32_t idx){
	level *res=(level*)calloc(1,sizeof(level));
	res->idx=idx;
	res->array=(run*)calloc(max_run_num, sizeof(run));
	if(istier){
		printf("test\n");
	}
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

uint32_t level_append_sstfile(level *lev, sst_file *sptr){
	if(lev->istier){
		EPRINT("it can't be", true);
		/*
		if(sptr->type!=BLOCK_FILE){
			EPRINT("tier must have block file", true);
		}
		if(lev->run_num >= lev->max_run_num){
			EPRINT("over run in level", true);
		}
		run_append_sstfile(&lev->array[lev->run_num], sptr);
		lev->run_num++;
		 */
	}
	else{
		run_append_sstfile(LAST_RUN_PTR(lev), sptr);
		lev->now_sst_num++;
		if(lev->now_sst_num >lev->max_sst_num){
			EPRINT("over sst file level", true);
			return 1;
		}
	}
	return 0;
}

uint32_t level_deep_append_sstfile(level *lev, sst_file *sptr){

	if(lev->istier){
		EPRINT("it can't be", true);
		/*
		if(sptr->type!=BLOCK_FILE){
			EPRINT("tier must have block file", true);
		}
		if(lev->run_num >= lev->max_run_num){
			EPRINT("over run in level", true);
		}
		run_deep_append_sstfile(&lev->array[lev->run_num], sptr);
		lev->run_num++;*/
	}
	else{
		if(lev->now_sst_num >lev->max_sst_num){
			EPRINT("over sst file level", true);
			return 1;
		}
		run_deep_append_sstfile(LAST_RUN_PTR(lev), sptr);
		lev->now_sst_num++;
	}
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

uint32_t level_append_run(level *lev, run *r){
	if(!lev->istier){
		EPRINT("it must be tiering level", true);
	}
	if(lev->run_num >= lev->max_run_num){
		EPRINT("over run in level", true);
	}

	run_copy_src_empty(&lev->array[lev->run_num++], r);
	return 1;
}

uint32_t level_deep_append_run(level *lev, run *r){
	if(!lev->istier){
		EPRINT("it must be tiering level", true);
	}
	if(lev->run_num >= lev->max_run_num){
		EPRINT("over run in level", true);
	}

	run_deep_copy(&lev->array[lev->run_num++], r);
	return 1;
}

sst_file* level_retrieve_sst(level *lev, uint32_t lba){
	if(lev->istier){
		EPRINT("Tier level not allowed!", true);
	}
	return run_retrieve_sst(&lev->array[0], lba);
}

sst_file* level_retrieve_close_sst(level *lev, uint32_t lba){
	if(lev->istier){
		EPRINT("Tier level not allowed!", true);
	}
	return run_retrieve_close_sst(&lev->array[0], lba);
}

static inline void level_destroy_content(level *lev){
	run *run_ptr;
	uint32_t idx;
	/*assigned level*/
	for_each_run(lev, run_ptr, idx){
		run_destroy_content(run_ptr);
	}
	/*unassigned level*/
	for(uint32_t i=lev->run_num; i<lev->max_run_num; i++){
		free(lev->array[i].sst_set);
	}
}

void level_free(level *lev){
	level_destroy_content(lev);
	free(lev->array);
	free(lev);
}

void level_print(level *lev){
	printf("level idx:%d sst:%u/%u start lba:%u~end lba:%u\n", 
			lev->idx,
			lev->now_sst_num, lev->max_sst_num,
			FIRST_RUN_PTR(lev)->start_lba,
			LAST_RUN_PTR(lev)->end_lba
			);
}
