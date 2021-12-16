#include "run.h"
#include "../../interface/interface.h"

extern lower_info *g_li;
extern L2P_bm *bm;

static void *__run_read_end_req(algo_req *req){
	if(req->type!=DATAR  && req->type!=MISSDATAR){
		EPRINT("not allowed type", true);
	}

	request *p_req=req->parents;
	map_read_param *param=(map_read_param*)req->param;
	param->oob_set=(uint32_t*)bm->segment_manager->get_oob(bm->segment_manager, req->ppa);
	uint32_t intra_offset=param->mf->oob_check(param->mf, param);
	if(intra_offset!=NOT_FOUND){
		memmove(&p_req->value->value[0], &p_req->value->value[intra_offset*LPAGESIZE], LPAGESIZE);
		p_req->end_req(p_req);
		param->mf->query_done(param->mf, param);
	}
	else{
		inf_assign_try(p_req);
	}
	free(req);
	return NULL;
}

static void __run_issue_read(request *req, uint32_t ppa, value_set *value, map_read_param *param, bool retry){
	algo_req *res=(algo_req*)calloc(1, sizeof(algo_req));
	res->parents=req;
	res->ppa=ppa;
	res->param=(void *)param;
	res->type=retry?MISSDATAR:DATAR;
	res->end_req=__run_read_end_req;
	g_li->read(ppa, PAGESIZE, value, res);
}


uint32_t run_translate_intra_offset(run *r, uint32_t intra_offset){
	return st_array_read_translation(r->st_body, intra_offset);
}

uint32_t run_query(run *r, request *req){
	req->retry=true;
	map_read_param *param;
	uint32_t intra_offset=r->mf->query(r->mf, req, &param);
	uint32_t psa=run_translate_intra_offset(r, intra_offset);
	if(psa==NOT_FOUND){
		param->mf->query_done(param->mf, param);
		return NOT_FOUND;
	}

	//DEBUG_CNT_PRINT(test, UINT32_MAX, __FUNCTION__, __LINE__);

	param->intra_offset=psa%L2PGAP;
	param->r=r;
	req->param=(void*)param;
	__run_issue_read(req, psa/L2PGAP, req->value, param, false);
	return 0;
}

uint32_t run_query_retry(run *r, request *req){
	map_read_param *param=(map_read_param*)req->param;
	uint32_t intra_offset=r->mf->query_retry(r->mf, param);
	uint32_t psa=st_array_read_translation(r->st_body, intra_offset);
	if(psa==NOT_FOUND){
		param->mf->query_done(param->mf, param);
		return NOT_FOUND;
	}
	param->intra_offset=psa%L2PGAP;
	req->param=(void*)param;
	__run_issue_read(req, psa/L2PGAP, req->value, param, true);
	return 0;
}
