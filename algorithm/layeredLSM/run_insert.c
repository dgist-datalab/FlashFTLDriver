#include "run.h"
#include "piece_ppa.h"

extern sc_master *shortcut;
extern lower_info *g_li;
extern uint32_t test_key;
extern uint32_t target_recency;
extern uint32_t test_piece_ppa;

static inline void __check_debug(run *r, uint32_t lba, uint32_t psa){
#ifdef LSM_DEBUG
	if(lba==test_key || psa==test_piece_ppa){
		if(psa==1535426){
			printf("break!\n");
		}
		if(lba==test_key && psa==test_piece_ppa){
			printf("break2\n");
		}
		printf("target insert (%u:%u, %u:%u):",test_key,lba, test_piece_ppa, psa);
		run_print(r, false);
	}
#endif
}

static void* __run_write_end_req(algo_req *req){
	if(req->param){//for summary_write
		st_array_summary_write_done((summary_write_param*)req->param);
	}
	else{
		inf_free_valueset(req->value, FS_MALLOC_W);
	}
	free(req);
	return NULL;
}

static void __run_issue_write(uint32_t ppa, value_set *value, char *oob_set, blockmanager *sm, void *param, uint32_t  type){
	algo_req *res=(algo_req*)malloc(sizeof(algo_req));
	res->type=type;
	res->ppa=ppa;
	res->value=value;
	res->end_req=__run_write_end_req;
	res->param=param;
	if(oob_set){
		sm->set_oob(sm, oob_set, sizeof(uint32_t) * L2PGAP, ppa);
	}
	g_li->write(ppa, PAGESIZE, value, res);
}

static inline void __run_insert_mf(run *r, blockmanager *sm, uint32_t lba, uint32_t intra_offset){
	uint32_t res=r->mf->insert(r->mf, lba, intra_offset);
	if(res!=INSERT_SUCCESS){
		res=run_translate_intra_offset(r, res);
		if(invalidate_piece_ppa(sm, res, true)!=BIT_SUCCESS){
			EPRINT("double delete error", true);
		}
		r->invalidate_piece_num++;
	}
}

static void __run_write_buffer(run *r, blockmanager *sm, bool force, 
		uint32_t type){
	uint32_t target_ppa, psa, intra_offset;
	uint32_t psa_list[L2PGAP];
	for(uint32_t i=0; i<r->pp->buffered_num; i++){
		intra_offset=r->st_body->write_pointer;
		psa=st_array_write_translation(r->st_body);
		psa_list[i]=psa;
		uint32_t lba=r->pp->LBA[i];

		if(i==0){
			target_ppa=psa/L2PGAP;
#ifdef LSM_DEBUG
			sm->set_oob(sm, (char*)r->pp->LBA, sizeof(uint32_t) * L2PGAP, target_ppa);
#endif
		}

		if(validate_piece_ppa(sm, psa_list[i], true)!=BIT_SUCCESS){
			EPRINT("double insert error", true);
		}
		r->validate_piece_num++;
		
		__check_debug(r, lba, psa);

		__run_insert_mf(r, sm, lba, intra_offset);
		if(r->type==RUN_LOG){
			st_array_insert_pair(r->st_body, lba, psa);
		}
		else{
			st_array_insert_pair(r->st_body, lba, intra_offset);
		}
	}
	__run_issue_write(target_ppa, pp_get_write_target(r->pp, force), (char*)r->pp->LBA, 
			sm, NULL, type);
}

static void __run_write_meta(run *r, blockmanager *sm, bool force){
	uint32_t target_ppa=st_array_summary_translation(r->st_body, force)/L2PGAP;
	summary_write_param *swp=st_array_get_summary_param(r->st_body, target_ppa, force);
	if(!swp) return;

	if(validate_ppa(sm, target_ppa, true)!=BIT_SUCCESS){
		EPRINT("map write error", true);
	}

	__run_issue_write(target_ppa, swp->value, (char*)swp->oob, 
			r->st_body->bm->segment_manager, (void*)swp, MAPPINGW);
}

bool run_insert(run *r, uint32_t lba, uint32_t psa, char *data, bool merge_insert){
	if(r->max_entry_num < r->now_entry_num){
		return false;
	}

	if(r->type==RUN_PINNING){
		if(data){
			EPRINT("not allowed in RUN_PINNING", true);
		}

		__check_debug(r, lba, psa);

		__run_insert_mf(r, r->st_body->bm->segment_manager, lba, r->st_body->write_pointer);
		st_array_insert_pair(r->st_body, lba, psa);
	}
	else{
		if(psa!=UINT32_MAX){
			EPRINT("not allowed in RUN_NORMAL", true);
		}
		if(!r->pp){
			r->pp=pp_init();
		}
		if(pp_insert_value(r->pp, lba, data)){
			__run_write_buffer(r, r->st_body->bm->segment_manager, false, merge_insert?COMPACTIONDATAW:DATAW);
			pp_reinit_buffer(r->pp);
		}
	}
	shortcut_unlink_and_link_lba(shortcut, r, lba);
	if(r->st_body->summary_write_alert){
		__run_write_meta(r, r->st_body->bm->segment_manager, false);
	}

	r->now_entry_num++;
	return true;
}

void run_insert_done(run *r, bool merge_insert){
	if(r->pp && r->pp->buffered_num!=0){
		__run_write_buffer(r, r->st_body->bm->segment_manager,true, merge_insert?COMPACTIONDATAW:DATAW);
	}
	__run_write_meta(r, r->st_body->bm->segment_manager, true);
	r->mf->make_done(r->mf);
	if(r->pp){
		pp_free(r->pp);
	}
	r->pp=NULL;
}
