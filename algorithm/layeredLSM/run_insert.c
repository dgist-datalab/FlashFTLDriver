#include "run.h"

extern lower_info *g_li;

static void* __run_write_end_req(algo_req *req){
	if(req->type!=DATAW){
		EPRINT("not allowed type", true);
	}

	if(req->param){//for summary_write
		st_array_summary_write_done((summary_write_param*)req->param);
	}
	else{
		inf_free_valueset(req->value, FS_MALLOC_W);
	}
	free(req);
	return NULL;
}

static void __run_issue_write(uint32_t ppa, value_set *value, char *oob_set, blockmanager *sm, void *param){
	algo_req *res=(algo_req*)malloc(sizeof(algo_req));
	res->type=DATAW;
	res->ppa=ppa;
	res->value=value;
	res->end_req=__run_write_end_req;
	res->param=param;
	if(oob_set){
		sm->set_oob(sm, oob_set, sizeof(uint32_t) * L2PGAP, ppa);
	}
	g_li->write(ppa, PAGESIZE, value, res);
}

static void __run_write_buffer(run *r, bool force){
	uint32_t target_ppa, psa, inter_offset;
	for(uint32_t i=0; i<r->pp->buffered_num; i++){
		inter_offset=r->st_body->write_pointer;
		psa=st_array_write_translation(r->st_body);
		uint32_t lba=r->pp->LBA[i];
		if(i==0) target_ppa=psa/L2PGAP;
		st_array_insert_pair(r->st_body, lba, psa);
		r->mf->insert(r->mf, lba, inter_offset);
	}
	__run_issue_write(target_ppa, pp_get_write_target(r->pp, force), (char*)r->pp->LBA, 
			r->st_body->bm->segment_manager, NULL);

}

static void __run_write_meta(run *r, bool force){
	static uint32_t temp_data[L2PGAP]={UINT32_MAX,UINT32_MAX,};
	uint32_t target_ppa=st_array_summary_translation(r->st_body, force)/L2PGAP;
	summary_write_param *swp=st_array_get_summary_param(r->st_body, target_ppa, force);
	if(!swp) return;
	__run_issue_write(target_ppa, swp->value, NULL, 
			r->st_body->bm->segment_manager, (void*)swp);
}

bool run_insert(run *r, uint32_t lba, char *data){
	if(r->max_entry_num < r->now_entry_num){
		return false;
	}

	if(!r->pp){
		r->pp=pp_init();
	}


	if(pp_insert_value(r->pp, lba, data)){
		__run_write_buffer(r, false);
		pp_reinit_buffer(r->pp);
	}

	if(r->st_body->summary_write_alert){
		__run_write_meta(r, false);
	}

	r->now_entry_num++;
	return true;
}

void run_insert_done(run *r){
	if(r->pp->buffered_num!=0){
		__run_write_buffer(r, true);
	}
	__run_write_meta(r, true);
	r->mf->make_done(r->mf);
	pp_free(r->pp);
	r->pp=NULL;
}
