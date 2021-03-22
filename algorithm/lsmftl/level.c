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

level *level_convert_run_to_lev(run *r, page_manager *pm){
	level *res=level_init(r->max_sst_file_num, 1, false, UINT32_MAX);
	sst_file *sptr, *new_sptr;
	map_range *map_ptr;
	uint32_t sidx, midx;
	for_each_sst(r, sptr, sidx){
		for_each_map_range(sptr, map_ptr, midx){
			new_sptr=sst_MR_to_sst_file(map_ptr);
			new_sptr->already_invalidate_file=sptr->already_invalidate_file;

			level_append_sstfile(res, new_sptr);
			sst_free(new_sptr, pm);
		}
	}
	return res;
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


sst_file* level_find_target_run_idx(level *lev, uint32_t lba, uint32_t piece_ppa, uint32_t *target_ridx){
	if(!lev->istier){
		EPRINT("tiering only",true);
	}

	run *run_ptr;
	uint32_t ridx;
	sst_file *sptr;
	for_each_run(lev, run_ptr, ridx){
		sptr=run_retrieve_sst(run_ptr, lba);
		if(sptr->file_addr.piece_ppa<=piece_ppa &&
				sptr->end_ppa*L2PGAP>=piece_ppa){
			*target_ridx=ridx;
			return sptr;
		}
	}
	return NULL;
}

static inline void level_destroy_content(level *lev, page_manager *pm){
	run *run_ptr;
	uint32_t idx;
	/*assigned level*/
	for_each_run(lev, run_ptr, idx){
		run_destroy_content(run_ptr, pm);
	}
	/*unassigned level*/
	for(uint32_t i=lev->run_num; i<lev->max_run_num; i++){
		free(lev->array[i].sst_set);
	}
}

void level_free(level *lev, page_manager *pm){
	level_destroy_content(lev, pm);
	free(lev->array);
	free(lev);
}

uint32_t level_update_run_at(level *lev, uint32_t idx, run *r, bool new_run){
	if(!lev->istier){
		EPRINT("it must be tiering level", true);
	}
	if(lev->run_num > lev->max_run_num){
		EPRINT("over run in level", true);
	}
	run_deep_copy(&lev->array[idx], r);
	if(new_run){
		lev->now_sst_num+=r->now_sst_file_num;
		lev->run_num++;
	}
	return 1;
}

void level_print(level *lev){
	if(lev->now_sst_num){
		if(lev->istier){
			printf("level idx:%d run %u/%u\n",
					lev->idx,
					lev->run_num, lev->max_run_num
				  );	
		}
		else{
			printf("level idx:%d sst:%u/%u start lba:%u~end lba:%u\n", 
					lev->idx,
					lev->now_sst_num, lev->max_sst_num,
					FIRST_RUN_PTR(lev)->start_lba,
					LAST_RUN_PTR(lev)->end_lba
				  );
		}
	}
	else{
		printf("level idx:%d sst:%u/%u\n", 
			lev->idx,
			lev->now_sst_num, lev->max_sst_num);
	}
}

