#include "run.h"
#include "../../interface/interface.h"
#include "./lsmtree.h"
#include "./piece_ppa.h"
#include "./cache_layer.h"
#include "../../include/utils/data_copy.h"
extern uint32_t test_key;
extern uint32_t test_key2;
extern lower_info *g_li;
extern lsmtree *LSM;
extern page_read_buffer rb;
extern MeasureTime lsmtree_mt;

static inline void __check_data(algo_req *req, char *value){
	request *p_req=req->parents;

	map_read_param *param=(map_read_param*)req->param;
	param->oob_set=(uint32_t*)LSM->bm->segment_manager->get_oob(LSM->bm->segment_manager, req->ppa);
	uint32_t intra_offset=param->mf->oob_check(param->mf, param);
	//uint32_t intra_offset=0;
	if(intra_offset!=NOT_FOUND){
		//data_move(p_req->value->value, &value[intra_offset*LPAGESIZE], LPAGESIZE);
		p_req->end_req(p_req);
		param->mf->query_done(param->mf, param);
	}
	else{
		inf_assign_try(p_req);
	}
	free(req);
}

typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;
static void *__run_read_end_req(algo_req *req){
	if(req->type!=DATAR  && req->type!=MISSDATAR){
	//	EPRINT("not allowed type", true);
	}

	rb_r_iter target_r_iter;
	algo_req *pending_req;

	//fdriver_lock(&rb.pending_lock);
	//target_r_iter=rb.pending_req->find(req->value->ppa);
	//for(;target_r_iter->first==req->value->ppa && 
	//				target_r_iter!=rb.pending_req->end();){
	//	pending_req=target_r_iter->second;
	//	map_read_param *param=(map_read_param*)pending_req->param;
	//	__check_data(pending_req, req->value->value);
	//	rb.pending_req->erase(target_r_iter++);
	//}
	//rb.issue_req->erase(req->value->ppa);
	//fdriver_unlock(&rb.pending_lock);

	//fdriver_lock(&rb.read_buffer_lock);
	//rb.buffer_ppa = req->value->ppa;
	//data_copy(rb.buffer_value, req->value->value, PAGESIZE);
	//fdriver_unlock(&rb.read_buffer_lock);

	__check_data(req, rb.buffer_value);;
	return NULL;
}

static void __run_issue_read(request *req, uint32_t ppa, value_set *value, map_read_param *param, bool retry){


	algo_req *res=(algo_req*)calloc(1, sizeof(algo_req));
	res->parents=req;
	res->ppa=ppa;
	res->param=(void *)param;
	res->type=retry?MISSDATAR:DATAR;
	res->end_req=__run_read_end_req;
	res->value=value;
	value->ppa=ppa;
	
	if(param->mf->type==COMP_MAP && !param->read_map && res->type==MISSDATAR){
		res->type=DATAR;
	}
	else if(param->mf->type==COMP_MAP && param->read_map){
		res->type=MAPPINGR;
	}



	//fdriver_lock(&rb.read_buffer_lock);
	//if (ppa == rb.buffer_ppa)
	//{
	//	data_copy(value->value, &rb.buffer_value, PAGESIZE);
	//	map_read_param *param=(map_read_param*)res->param;

	//	req->buffer_hit++;
	//	__check_data(res, rb.buffer_value);
	//	fdriver_unlock(&rb.read_buffer_lock);
	//	//issue_time=time_breakdown_end();
	//	//LSM->monitor.issue_time+=issue_time;
	//	//LSM->monitor.issue_cnt++;
	//	return;
	//}
	//fdriver_unlock(&rb.read_buffer_lock);


	//fdriver_lock(&rb.pending_lock);
	//rb_r_iter temp_r_iter = rb.issue_req->find(ppa);
	//if (temp_r_iter == rb.issue_req->end())
	//{
	//	rb.issue_req->insert(std::pair<uint32_t, algo_req *>(ppa, res));
	//	fdriver_unlock(&rb.pending_lock);
	//}
	//else
	//{
	//	req->buffer_hit++;
	//	rb.pending_req->insert(std::pair<uint32_t, algo_req *>(ppa, res));
	//	fdriver_unlock(&rb.pending_lock);
	//	//issue_time=time_breakdown_end();
	//	//LSM->monitor.issue_time+=issue_time;
	//	//LSM->monitor.issue_cnt++;
	//	return;
	//}
	uint64_t issue_time=0;
	time_breakdown_start();
	g_li->read(ppa, PAGESIZE, value, res);

	issue_time=time_breakdown_end();
	LSM->monitor.issue_time+=issue_time;
	LSM->monitor.issue_cnt++;

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

uint32_t run_pick_target_mf(run *r, uint32_t lba, map_function **mf){
	uint32_t ste_num = st_array_get_target_STE(r->st_body, lba);
	uint32_t res=r->st_body->sp_meta[ste_num].piece_ppa;
	(*mf)=r->st_body->pba_array[ste_num].mf;
	return res;
}

uint32_t run_query(run *r, request *req){
	if(r->pp){
		char *res=pp_find_value(r->pp, req->key);
		if (res){
			data_copy(req->value->value, res, LPAGESIZE);
			req->end_req(req);
			return READ_DONE;
		}
	}

	req->retry=true;

	uint32_t ste_num;
	map_read_param *param;
	map_function *mf;
	uint32_t psa;

	if(r->type==RUN_LOG){
		mf=r->run_log_mf;
		uint32_t global_intra_offset=mf->query_by_req(mf, req, &param);
		if(global_intra_offset==NOT_FOUND){
			param->mf->query_done(param->mf, param);
			goto not_found_end;
		}
		else{
			ste_num=global_intra_offset;
			psa=run_translate_intra_offset(r, UINT32_MAX, global_intra_offset);
			req->type_ftl+=10;
		}
	}
	else{
		uint64_t bs_search_time=0;
		uint64_t map_search_time=0;

		time_breakdown_start();
		ste_num = st_array_get_target_STE(r->st_body, req->key);
		bs_search_time=time_breakdown_end();

		if (ste_num == UINT32_MAX)
		{
			goto not_found_end;
		}
		req->retry = true;
		mf = r->st_body->pba_array[ste_num].mf;

		time_breakdown_start();
		uint32_t intra_offset = mf->query_by_req(mf, req, &param);
		map_search_time=time_breakdown_end();

		switch(mf->type){
			case BF:
			case GUARD_BF:
			req->type_ftl+=20;
			LSM->monitor.binary_search_cnt[0]++;
			LSM->monitor.binary_search_time[0]+=bs_search_time;

			LSM->monitor.bf_cnt++;
			LSM->monitor.bf_time+=map_search_time;
			break;
			case PLR_MAP:
			req->type_ftl+=30;
			LSM->monitor.binary_search_cnt[1]++;
			LSM->monitor.binary_search_time[1]+=bs_search_time;

			LSM->monitor.plr_cnt++;
			LSM->monitor.plr_time+=map_search_time;
			break;
		}
retry:
		if(intra_offset==READ_MAP){
			goto read_map;
		}
		if (intra_offset == NOT_FOUND)
		{
			param->mf->query_done(param->mf, param);
			goto not_found_end;
		}
		psa = run_translate_intra_offset(r, ste_num, intra_offset);
		if (psa == UNLINKED_PSA)
		{
			intra_offset=mf->query_retry(mf, param);
			goto retry;
		}
	}

read_map:
	//fdriver_lock(&LSM->read_cnt_lock);
	//LSM->now_flying_read_cnt++;
	//fdriver_unlock(&LSM->read_cnt_lock);

	if(param->read_map){
		param->intra_offset=r->st_body->sp_meta[ste_num].piece_ppa %L2PGAP;
		param->ste_num=ste_num;
		param->r=r;
		param->psa=r->st_body->sp_meta[ste_num].piece_ppa;
		req->param=(void*)param;
		__run_issue_read(req, r->st_body->sp_meta[ste_num].piece_ppa/L2PGAP, req->value, param, false);
	}
	else{
		param->intra_offset=psa%L2PGAP;
		param->ste_num=ste_num;
		param->r=r;
		req->param=(void*)param;
		__run_issue_read(req, psa/L2PGAP, req->value, param, false);
	}
	return READ_DONE;

not_found_end:
	return READ_NOT_FOUND;
}

uint32_t run_query_retry(run *r, request *req){
	if(r->type==RUN_LOG){
		return READ_NOT_FOUND;
	}

	map_read_param *param=(map_read_param*)req->param;
	if(param->mf->type==COMP_MAP){
		param->read_map=false;
	}
	uint32_t ste_num=param->ste_num;
	map_function *mf=r->st_body->pba_array[ste_num].mf;
retry:
	uint64_t retry_time=0;
	time_breakdown_start();
	uint32_t intra_offset=mf->query_retry(mf, param);
	retry_time=time_breakdown_end();

	switch(mf->type){
		case BF:
		case GUARD_BF:
		LSM->monitor.bf_retry_cnt++;
		LSM->monitor.bf_retry_time+=retry_time;
		break;
		case PLR_MAP:
		LSM->monitor.plr_retry_cnt++;
		LSM->monitor.plr_retry_time+=retry_time;
		break;
	}


	if(intra_offset==NOT_FOUND){
		param->mf->query_done(param->mf, param);
		//fdriver_lock(&LSM->read_cnt_lock);
		//LSM->now_flying_read_cnt--;
		//fdriver_unlock(&LSM->read_cnt_lock);
		return READ_NOT_FOUND;
	}
	uint32_t psa=st_array_read_translation(r->st_body, ste_num, intra_offset);
	if(psa==UNLINKED_PSA){
		goto retry;
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
	uint32_t intra_offset=NOT_FOUND;
	map_function *mf;
	map_read_param *param;
	uint32_t t_psa;
	if(r->type==RUN_LOG){
		mf=r->run_log_mf;
		intra_offset=mf->query(mf, lba, &param);
		if(intra_offset==UINT32_MAX){
			goto out;
		}
		t_psa=run_translate_intra_offset(r, UINT32_MAX, intra_offset);

		if(t_psa==psa){
			*_ste_num=intra_offset/MAX_SECTOR_IN_BLOCK;
			intra_offset%=MAX_SECTOR_IN_BLOCK;
			goto out;
		}
		else{
			intra_offset=NOT_FOUND;
			goto out;
		}
	}
	else{
		uint32_t ste_num = st_array_get_target_STE(r->st_body, lba);
		if (ste_num == UINT32_MAX)
		{
			return NOT_FOUND;
		}
		*_ste_num = ste_num;
		mf = r->st_body->pba_array[ste_num].mf;
		intra_offset = mf->query(mf, lba, &param);
		if (intra_offset == UINT32_MAX)
		{
			goto out;
		}
		t_psa = st_array_read_translation(r->st_body, ste_num, intra_offset);
		while (t_psa != psa)
		{
			intra_offset = mf->query_retry(mf, param);
			if (intra_offset == NOT_FOUND)
			{
				break;
			}
			t_psa = st_array_read_translation(r->st_body, ste_num, intra_offset);
		}
	}
out:
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

run *run_find_include_address_for_mixed(sc_master *sc, uint32_t lba, uint32_t psa,
		uint32_t *_ste_num, uint32_t *_intra_offset){
	run *r=shortcut_query(sc, lba);
	if(r->type==RUN_NORMAL){
		uint32_t ste_num=st_array_get_target_STE(r->st_body, lba);
		*_ste_num=ste_num;
		*_intra_offset=NOT_FOUND;
	}
	else{
		uint32_t intra_offset=__find_target_in_run(r, lba, psa, _ste_num);
		*_intra_offset=intra_offset;
		if(intra_offset==NOT_FOUND){
			return NULL;
		}
	}
	return r;
}

uint32_t run_find_include_address_byself(run *r, uint32_t lba, uint32_t psa, uint32_t *_ste_num){
	return __find_target_in_run(r, lba, psa, _ste_num);
}
