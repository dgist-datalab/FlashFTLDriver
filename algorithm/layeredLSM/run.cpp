#include "run.h"

static inline run *__run_init(uint32_t run_idx, uint32_t limit_entry_num, uint32_t entry_num){
	run *res=(run*)malloc(sizeof(run));
	res->run_idx=run_idx;
	res->max_entry_num=entry_num;
	res->limit_entry_num=limit_entry_num;
	res->now_entry_num=0;
	res->pp=NULL;
	res->info=NULL;
	fdriver_mutex_init(&res->lock);
	return res;
}

run *run_factory(uint32_t run_idx, uint32_t map_type, 
	uint32_t entry_num, float fpr, L2P_bm *bm, uint32_t type, lsmtree *lsm){
	run *res=__run_init(run_idx, entry_num+SC_PER_DIR, entry_num);
	map_param param;
	param.fpr=fpr;
	param.lba_bit=lsm->param.target_bit;
	param.map_type=map_type;
	res->type=type;
	switch(type){
		case RUN_NORMAL:
			res->st_body=st_array_init(res, entry_num, bm, false, param);
			break;
		case RUN_LOG:
			param.map_type=TREE_MAP;
			res->run_log_mf=map_function_factory(param, entry_num+SC_PER_DIR);
			param.map_type=map_type;
			res->st_body=st_array_init(res, entry_num+SC_PER_DIR, bm, true, param);
			break;
		case RUN_PINNING:
			if(map_type==PLR_MAP){
				EPRINT("PLR_MAP is not enable on RUN_PINNING type", true);
			}
			res->st_body=st_array_init(res, entry_num, bm, true, param);
			break;
	}
	res->lsm=lsm;
	return res;
}

void run_free(run *r ,struct shortcut_master *sc){
	fdriver_lock(&r->lock);
	if(r->pp){
		if(r->type!=RUN_LOG){
			EPRINT("what happened?", true);
		}
		else{
			pp_free(r->pp);
		}
	}
	shortcut_release_sc_info(sc, r->info->idx);
	uint64_t memory_usage_bit=0;
	memory_usage_bit=run_memory_usage(r, r->lsm->param.target_bit);
	uint32_t map_type=r->type==RUN_LOG?r->run_log_mf->type:r->st_body->param.map_type;
	__lsm_calculate_memory_usage(r->lsm, r->now_entry_num, -1 * memory_usage_bit, map_type, r->type==RUN_PINNING);
	st_array_free(r->st_body);
	if(r->type==RUN_LOG){
		r->run_log_mf->free(r->run_log_mf);
	}
	fdriver_unlock(&r->lock);
	free(r);
}