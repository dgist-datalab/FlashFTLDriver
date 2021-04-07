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
		run_append_sstfile_move_originality(LAST_RUN_PTR(lev), sptr);
		lev->now_sst_num++;
		if(lev->now_sst_num >lev->max_sst_num){
			EPRINT("over sst file level", true);
			return 1;
		}
	}
	return 0;
}

level *level_convert_run_to_lev(run *r, page_manager *pm){
	sst_file *sptr, *new_sptr;
	map_range *map_ptr;
	uint32_t sidx, midx;
	uint32_t target_sst_file_num=0;
	for_each_sst(r, sptr, sidx){
		target_sst_file_num+=sptr->map_num;
	}
	level *res=level_init(target_sst_file_num, 1, false, UINT32_MAX);
	for_each_sst(r, sptr, sidx){
		for_each_map_range(sptr, map_ptr, midx){
			new_sptr=sst_MR_to_sst_file(map_ptr);
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

uint32_t level_append_run_copy_move_originality(level *lev, run *r, uint32_t ridx){
	if(!lev->istier){
		EPRINT("it must be tiering level", true);
	}
	if(lev->run_num >= lev->max_run_num){
		EPRINT("over run in level", true);
	}

	run_shallow_copy_move_originality(&lev->array[ridx], r);
	lev->run_num++;
	//run_copy_src_empty(&lev->array[lev->run_num++], r);
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

sst_file* level_retrieve_sst_with_check(level *lev, uint32_t lba){
	if(lev->istier){
		EPRINT("Tier level not allowed!", true);
	}
	sst_file *res=run_retrieve_sst(&lev->array[0], lba);
	uint32_t temp;
	if(!res) return NULL;
	if(res->_read_helper){
		uint32_t helper_idx=read_helper_idx_init(res->_read_helper, lba);
		if(read_helper_check(res->_read_helper, lba, &temp, res, &helper_idx)){
			return res;	
		}
		else
			return NULL;
	}
	return res;
}

sst_file* level_find_target_run_idx(level *lev, uint32_t lba, uint32_t piece_ppa, uint32_t *target_ridx, uint32_t *sptr_idx){
	if(!lev->istier){
		EPRINT("tiering only",true);
	}

	run *run_ptr;
	uint32_t ridx;
	sst_file *sptr;
	for_each_run_max(lev, run_ptr, ridx){
		if(run_ptr->now_sst_file_num==0) continue;
		sptr=run_retrieve_sst(run_ptr, lba);
		if(sptr && sptr->file_addr.piece_ppa<=piece_ppa &&
				sptr->end_ppa*L2PGAP>=piece_ppa){
			*target_ridx=ridx;
			*sptr_idx=(sptr-run_ptr->sst_set);
			return sptr;
		}
	}
	return NULL;
}

static inline void level_destroy_content(level *lev, page_manager *pm){
	if(!lev) return;
	run *run_ptr;
	uint32_t idx;
	/*assigned level*/
	for_each_run_max(lev, run_ptr, idx){
		if(run_ptr->now_sst_file_num){
			run_destroy_content(run_ptr, pm);
		}
	}
	for_each_run_max(lev, run_ptr, idx){
		if(!run_ptr->now_sst_file_num){
			free(run_ptr->sst_set);
		}
	}
}

void level_free(level *lev, page_manager *pm){
	level_destroy_content(lev, pm);
	free(lev->array);
	free(lev);
}

uint32_t level_update_run_at_move_originality(level *lev, uint32_t idx, run *r, bool new_run){
	if(!lev->istier){
		EPRINT("it must be tiering level", true);
	}
	if(lev->run_num > lev->max_run_num){
		EPRINT("over run in level", true);
	}
	run_shallow_copy_move_originality(&lev->array[idx], r);
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

void level_content_print(level *lev, bool print_sst){
	run *rptr;
	uint32_t ridx;
	sst_file *sptr;
	uint32_t sidx;
	level_print(lev);
	for_each_run(lev, rptr, ridx){
		printf("\t[%u] ",ridx);
		run_print(rptr);
		if(print_sst){
			for_each_sst(rptr, sptr, sidx){
				printf("\t\t[%u] ",sidx);
				sst_print(sptr);
			}
		}
	}
}

void level_run_reinit(level *lev, uint32_t idx){
	lev->run_num--;
	run_reinit(&lev->array[idx]);
}

void level_sptr_update_in_gc(level *lev, uint32_t ridx, uint32_t sptr_idx, sst_file *sptr){
	if(!lev->istier){
		EPRINT("only tier available", true);
	}
	
	sst_file *org_sptr=&lev->array[ridx].sst_set[sptr_idx];
	/*free(org_sptr->block_file_map);
	read_helper_free(org_sptr->_read_helper);*/
	sst_reinit(org_sptr);

	lev->array[ridx].sst_set[sptr_idx]=(*sptr);

	sst_file *nxt_sstfile, *prev_sstfile;
	if(sptr_idx==0){
		if(lev->array[ridx].now_sst_file_num -1 >=sptr_idx+1){
			nxt_sstfile=&lev->array[ridx].sst_set[sptr_idx+1];
			if(sptr->end_lba < nxt_sstfile->start_lba){}
			else{
				EPRINT("range error", true);
			}
		}
	}
	else{
		prev_sstfile=&lev->array[ridx].sst_set[sptr_idx-1];

		if(lev->array[ridx].now_sst_file_num -1 >= sptr_idx+1){
			nxt_sstfile=&lev->array[ridx].sst_set[sptr_idx+1];
			if(sptr->end_lba < nxt_sstfile->start_lba){}
			else{
				EPRINT("range error", true);
			}
		}

		if(prev_sstfile->end_lba < sptr->start_lba){}
		else{
			EPRINT("range error", true);
		}
	}
}

void level_sptr_add_at_in_gc(level *lev, uint32_t ridx, uint32_t sptr_idx, sst_file *sptr){
	if(!lev->istier){
		EPRINT("only tier available", true);
	}

	/*range check*/
	sst_file *nxt_sstfile, *prev_sstfile;
	if(sptr_idx==0){
		if(lev->array[ridx].now_sst_file_num -1 >= sptr_idx+1){
			nxt_sstfile=&lev->array[ridx].sst_set[sptr_idx+1];
			if(sptr->end_lba < nxt_sstfile->start_lba){}
			else{
				EPRINT("range error", true);
			}
		}
	}
	else{
		prev_sstfile=&lev->array[ridx].sst_set[sptr_idx-1];

		if(lev->array[ridx].now_sst_file_num-1 >= sptr_idx+1){
			nxt_sstfile=&lev->array[ridx].sst_set[sptr_idx+1];
			if(sptr->end_lba < nxt_sstfile->start_lba){}
			else{
				EPRINT("range error", true);
			}
		}
		else if(lev->array[ridx].now_sst_file_num == sptr_idx){
		
		}

		if(prev_sstfile->end_lba < sptr->start_lba){}
		else{
			EPRINT("range error", true);
		}
	}


	uint32_t target_num=lev->array[ridx].now_sst_file_num-sptr_idx;
	if(sptr_idx+1+target_num >= lev->array[ridx].max_sst_file_num){
		run *now_r=&lev->array[ridx];
		sst_file *new_sst_set;
		new_sst_set=(sst_file*)calloc(now_r->max_sst_file_num+1, sizeof(sst_file));
		memcpy(new_sst_set, now_r->sst_set, sizeof(sst_file)*now_r->max_sst_file_num);
		free(now_r->sst_set);

		now_r->max_sst_file_num++;
		now_r->sst_set=new_sst_set;
		//		run_print(&lev->array[ridx]);
		//		EPRINT("over run", true);
	}

	if(sptr_idx<=lev->array[ridx].now_sst_file_num-1){
		memmove(&lev->array[ridx].sst_set[sptr_idx+1],&lev->array[ridx].sst_set[sptr_idx], sizeof(sst_file) *target_num);
	}
	else if(sptr_idx!=lev->array[ridx].now_sst_file_num){
		EPRINT("how can't be", true);
	}

	//free(org_sptr->block_file_map);
	lev->array[ridx].sst_set[sptr_idx]=(*sptr);
	lev->array[ridx].now_sst_file_num++;
}

void level_sptr_remove_at_in_gc(level *lev, uint32_t ridx, uint32_t sptr_idx){
	if(!lev->istier){
		EPRINT("only tier available", true);
	}
	
	run *r=&lev->array[ridx];
	if(r->now_sst_file_num==0 || r->now_sst_file_num <=sptr_idx){
		EPRINT("?????", true);
	}

	uint32_t target_num_sst_file=r->now_sst_file_num-sptr_idx-1;
	memmove(&r->sst_set[sptr_idx], &r->sst_set[sptr_idx+1], sizeof(sst_file) * target_num_sst_file);
	memset(&r->sst_set[r->now_sst_file_num-1], 0, sizeof(sst_file));
	r->now_sst_file_num--;
}

