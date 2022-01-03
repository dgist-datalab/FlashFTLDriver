#include "run.h"
#include "piece_ppa.h"

extern lower_info *g_li;
extern uint32_t test_key;
extern uint32_t target_recency;
extern uint32_t test_piece_ppa;

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

static void __run_write_buffer(run *r, blockmanager *sm, bool force, 
		uint32_t type){
	uint32_t target_ppa=UINT32_MAX, psa, intra_offset;
	uint32_t psa_list[L2PGAP];
	for(uint32_t i=0; i<r->pp->buffered_num; i++){
		intra_offset=r->st_body->global_write_pointer;
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

		if (r->type == RUN_LOG){
			uint32_t res=r->run_log_mf->insert(r->run_log_mf, lba, intra_offset);
			if(res!=INSERT_SUCCESS){
				uint32_t old_psa=st_array_convert_global_offset_to_psa(r->st_body, res);
				if(invalidate_piece_ppa(r->st_body->bm->segment_manager, old_psa, true)!=BIT_SUCCESS){
					EPRINT("double delete error", true);
				}
			}
		}

		st_array_insert_pair(r->st_body, lba, psa);
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

bool run_insert(run *r, uint32_t lba, uint32_t psa, char *data, 
	bool merge_insert,	sc_master *shortcut){
	if(r->max_entry_num < r->now_entry_num){
		EPRINT("run full!", true);
		return false;
	}
	if(!shortcut_validity_check_and_link(shortcut, r, lba)){
		return false;
	}

	if(r->type==RUN_PINNING){
		if(data){
			EPRINT("not allowed in RUN_PINNING", true);
		}

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
	if(r->st_body->summary_write_alert){
		__run_write_meta(r, r->st_body->bm->segment_manager, false);
	}

	r->now_entry_num++;
	return true;
}

void run_padding_current_block(run *r){
	if(st_array_force_skip_block(r->st_body)==0){
		return;
	}
	if(r->st_body->summary_write_alert){
		__run_write_meta(r, r->st_body->bm->segment_manager, false);
	}
	else{
		EPRINT("not allowed", true);
	}
}

void run_copy_ste_to(run *r, struct sorted_table_entry *ste, summary_page_meta *spm, map_function *mf, bool unlinked_data_copy){
	st_array_copy_STE(r->st_body, ste, spm, mf, unlinked_data_copy);
}

void run_copy_unlinked_flag_update(run *r, uint32_t ste_num, bool flag){
	r->st_body->sp_meta[ste_num].unlinked_data_copy=flag;
}

void run_insert_done(run *r, bool merge_insert){
	if(r->pp && r->pp->buffered_num!=0){
		__run_write_buffer(r, r->st_body->bm->segment_manager,true, merge_insert?COMPACTIONDATAW:DATAW);
	}
	__run_write_meta(r, r->st_body->bm->segment_manager, true);

	if(r->pp){
		pp_free(r->pp);
	}
	r->pp=NULL;

	uint64_t mf_memory_usage=run_memory_usage(r, r->lsm->param.target_bit);
	uint32_t map_type=r->type==RUN_LOG?r->run_log_mf->type:r->st_body->param.map_type;
	__lsm_calculate_memory_usage(r->lsm,r->now_entry_num, mf_memory_usage, map_type, r->type==RUN_PINNING);
}
