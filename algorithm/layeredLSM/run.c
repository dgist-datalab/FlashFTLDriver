#include "run.h"

static inline run *__run_init(uint32_t map_type, uint32_t entry_num, float fpr, L2P_bm *bm){
	run *res=(run*)malloc(sizeof(run));
	res->max_entry_num=entry_num;
	res->now_entry_num=0;
	res->mf=map_function_factory(map_type, entry_num, fpr, 48);
	res->pp=NULL;
	res->info=NULL;
	res->validate_piece_num=res->invalidate_piece_num=0;
	return res;
}

run *run_factory(uint32_t map_type, uint32_t entry_num, float fpr, L2P_bm *bm, uint32_t type){
	run *res=__run_init(map_type, entry_num, fpr, bm);
	res->type=type;
	switch(type){
		case RUN_NORMAL:
			res->st_body=st_array_init(res, entry_num, bm, false);
			break;
		case RUN_PINNING:
			if(map_type==PLR_MAP){
				EPRINT("PLR_MAP is not enable on RUN_PINNING type", true);
			}
			res->st_body=st_array_init(res, entry_num, bm, true);
			break;
	}
	return res;
}

void run_free(run *r ,struct shortcut_master *sc){
	if(r->pp){
		EPRINT("what happened?", true);
	}
	shortcut_release_sc_info(sc, r->info->idx);
	r->mf->free(r->mf);
	st_array_free(r->st_body);
	free(r);
}
