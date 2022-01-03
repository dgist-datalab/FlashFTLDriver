#include "run.h"
#include "../../interface/interface.h"
#include "./lsmtree.h"
extern uint32_t test_key;
extern lower_info *g_li;
extern lsmtree *LSM;
static void *__run_read_end_req(algo_req *req){
	if(req->type!=DATAR  && req->type!=MISSDATAR){
		EPRINT("not allowed type", true);
	}

	request *p_req=req->parents;
	map_read_param *param=(map_read_param*)req->param;
	param->oob_set=(uint32_t*)LSM->bm->segment_manager->get_oob(LSM->bm->segment_manager, req->ppa);
	uint32_t intra_offset=param->mf->oob_check(param->mf, param);
	if(intra_offset!=NOT_FOUND){
		memmove(&p_req->value->value[0], &p_req->value->value[intra_offset*LPAGESIZE], LPAGESIZE);
		p_req->end_req(p_req);
	//	fdriver_unlock(&param->r->lock);
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

uint32_t run_translate_intra_offset(run *r, uint32_t ste_num, uint32_t intra_offset){
	if(r->type==RUN_LOG){
		//when the r type is log, the intra_offset counts the place over the STE.
		return st_array_read_translation(r->st_body, intra_offset/MAX_SECTOR_IN_BLOCK, intra_offset%MAX_SECTOR_IN_BLOCK);
	}
	else{
		return st_array_read_translation(r->st_body, ste_num, intra_offset);
	}
}

uint32_t run_query(run *r, request *req){
	//fdriver_lock(&r->lock);
	if(r->pp){
		char *res=pp_find_value(r->pp, req->key);
		if (res){
			memcpy(req->value->value, res, LPAGESIZE);
			//fdriver_unlock(&r->lock);
			req->end_req(req);
			return READ_DONE;
		}
	}

	if(req->key==test_key){
		GDB_MAKE_BREAKPOINT;
	}

	req->retry=true;

	uint32_t ste_num;
	map_read_param *param;
	map_function *mf;
	uint32_t psa;

	//printf("req->key:%u\n", req->key);
	if(r->type==RUN_LOG){
		mf=r->run_log_mf;
		uint32_t global_intra_offset=mf->query_by_req(mf, req, &param);
		if(global_intra_offset==NOT_FOUND){
			param->mf->query_done(param->mf, param);
			//fdriver_unlock(&param->r->lock);
			return READ_NOT_FOUND;
		}
		else{
			ste_num=global_intra_offset;
			psa=run_translate_intra_offset(r, UINT32_MAX, global_intra_offset);
		}
	}
	else{
		ste_num = st_array_get_target_STE(r->st_body, req->key);
		if (ste_num == UINT32_MAX)
		{
			//fdriver_unlock(&r->lock);
			return READ_NOT_FOUND;
		}
		req->retry = true;
		mf = r->st_body->pba_array[ste_num].mf;
		uint32_t intra_offset = mf->query_by_req(mf, req, &param);
		if (intra_offset == NOT_FOUND)
		{
			param->mf->query_done(param->mf, param);
			//fdriver_unlock(&r->lock);
			return READ_NOT_FOUND;
		}
		psa = run_translate_intra_offset(r, ste_num, intra_offset);
		if (psa == UNLINKED_PSA)
		{
			EPRINT("shortcut error", true);
		}
	}
	//DEBUG_CNT_PRINT(test, UINT32_MAX, __FUNCTION__, __LINE__);

	param->intra_offset=psa%L2PGAP;
	param->ste_num=ste_num;
	param->r=r;
	req->param=(void*)param;
	__run_issue_read(req, psa/L2PGAP, req->value, param, false);
	return READ_DONE;
}

uint32_t run_query_retry(run *r, request *req){
	if(r->type==RUN_LOG){
		return READ_NOT_FOUND;
	}
	map_read_param *param=(map_read_param*)req->param;
	uint32_t ste_num=param->ste_num;
	map_function *mf=r->st_body->pba_array[ste_num].mf;
	uint32_t intra_offset=mf->query_retry(mf, param);
	if(intra_offset==NOT_FOUND){
		param->mf->query_done(param->mf, param);
		//fdriver_unlock(&param->r->lock);
		return READ_NOT_FOUND;
	}
	uint32_t psa=st_array_read_translation(r->st_body, ste_num, intra_offset);
	if(psa==UNLINKED_PSA){
		EPRINT("shortcut error", true);
	}

	param->intra_offset=psa%L2PGAP;
	req->param=(void*)param;
	__run_issue_read(req, psa/L2PGAP, req->value, param, true);
	return 0;
}


static inline uint32_t __find_target_in_run(run *r, uint32_t lba, uint32_t psa, uint32_t *_ste_num){
	if(r->type==RUN_NORMAL){
		return NOT_FOUND;
	}

	uint32_t ste_num=st_array_get_target_STE(r->st_body, lba);
	if(ste_num==UINT32_MAX){
		return NOT_FOUND;
	}
	*_ste_num=ste_num;
	map_function *mf=r->st_body->pba_array[ste_num].mf;
	map_read_param *param;
	uint32_t intra_offset=mf->query(mf, lba, &param);
	if(intra_offset==UINT32_MAX){
		return NOT_FOUND;
	}
	uint32_t t_psa=st_array_read_translation(r->st_body, ste_num, intra_offset);
	if(r->type==RUN_LOG && t_psa!=psa){
		return NOT_FOUND;
	}
	while(t_psa!=psa){
		intra_offset=mf->query_retry(mf, param);
		if(intra_offset==NOT_FOUND){
			break;
		}
		t_psa=st_array_read_translation(r->st_body, ste_num, intra_offset);
	}
	free(param);
	return intra_offset;
}

run *run_find_include_address(sc_master *sc, uint32_t lba, uint32_t psa,
uint32_t *_ste_num, uint32_t *_intra_offset){
	run *r=shortcut_query(sc, lba);
	uint32_t intra_offset=__find_target_in_run(r, lba, psa, _ste_num);
	*_intra_offset=intra_offset;
	if(intra_offset==NOT_FOUND){
		return NULL;
	}
	return r;
}

uint32_t run_find_include_address_byself(run *r, uint32_t lba, uint32_t psa, uint32_t *_ste_num){
	return __find_target_in_run(r, lba, psa, _ste_num);
}
