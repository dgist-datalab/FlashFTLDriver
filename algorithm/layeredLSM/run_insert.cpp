#include "run.h"
#include "piece_ppa.h"
#include "../../include/sem_lock.h"

extern lower_info *g_li;
extern uint32_t test_key;
extern uint32_t test_key2;
extern uint32_t target_recency;
extern uint32_t test_piece_ppa;

static void* __run_write_end_req(algo_req *req){
	if(req->param){//for summary_write
		st_array_summary_write_done((summary_write_param*)req->param);
		inf_free_valueset(req->value, FS_MALLOC_W);
	}
	else{
		inf_free_valueset(req->value, FS_MALLOC_W);
	}

	if(req->parents){
		req->parents->end_req(req->parents);
	}

	free(req);
	return NULL;
}

static void __run_issue_write(uint32_t ppa, value_set *value, char *oob_set, blockmanager *sm, void *param, uint32_t  type, request *preq){
	algo_req *res=(algo_req*)malloc(sizeof(algo_req));
	res->parents=preq;
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

static void __run_write_meta(run *r, blockmanager *sm, bool force){
	value_set *value;
	uint32_t oob[L2PGAP];
	if(!st_check_swp(r->st_body)){
		uint32_t ppa;
		if((value=st_get_remain(r->st_body, oob, &ppa))){
			__run_issue_write(ppa, value, (char*)oob, 
				r->st_body->bm->segment_manager, NULL, MAPPINGW, NULL);
		}
		return;
	}
	static int cnt=0;
	uint32_t target_piece_ppa=st_array_summary_translation(r->st_body, force);
	summary_write_param *swp=st_array_get_summary_param(r->st_body, target_piece_ppa, force);


	if(validate_piece_ppa(sm, target_piece_ppa, true)!=BIT_SUCCESS){
		EPRINT("map write error", true);
	}
	if((value=st_swp_write(r->st_body,swp, oob, force))){
		__run_issue_write(target_piece_ppa/L2PGAP, value, (char*)oob, 
			r->st_body->bm->segment_manager, (void*)swp, MAPPINGW, NULL);
	}
}

static void __run_write_buffer(run *r, blockmanager *sm, bool force, 
		uint32_t type, request *preq){
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

		st_array_insert_pair(r->st_body, lba, psa, false);
		if(r->st_body->unaligned_block_write && r->st_body->summary_write_alert){
			__run_write_meta(r, r->st_body->bm->segment_manager, false);
		}
	}
	__run_issue_write(target_ppa, pp_get_write_target(r->pp, force), (char*)r->pp->LBA, 
			sm, NULL, type, preq);
}

uint32_t run_insert(run *r, uint32_t lba, uint32_t psa, char *data, 
	uint32_t io_type,	sc_master *shortcut, request *preq){
	uint32_t res=1;
	if(r->limit_entry_num < r->now_entry_num){
		EPRINT("run full!", true);
		res=0;
		goto out;
	}

	if(r->type==RUN_LOG && !shortcut_validity_check_and_link(shortcut, r, r, lba)){
		res=0;
		goto out;
	}

	if(r->type==RUN_PINNING){
		if(data){
			EPRINT("not allowed in RUN_PINNING", true);
		}
		st_array_insert_pair(r->st_body, lba, psa, false);
	}
	else{
		if(psa!=UINT32_MAX){
			EPRINT("not allowed in RUN_NORMAL", true);
		}
		if(!r->pp){
			r->pp=pp_init();
		}
		if(pp_insert_value(r->pp, lba, data)){
			__run_write_buffer(r, r->st_body->bm->segment_manager, false, io_type, preq);
			pp_reinit_buffer(r->pp);
			res=2;
		}
	}
	if(r->st_body->summary_write_alert){
		__run_write_meta(r, r->st_body->bm->segment_manager, false);
		res=2;
	}

	r->now_entry_num++;
out:
	return res;
}

void run_padding_current_block(run *r){
	if(r->pp && r->pp->buffered_num!=0){
		__run_write_buffer(r, r->st_body->bm->segment_manager,true, COMPACTIONDATAW, NULL);
		pp_reinit_buffer(r->pp);
	}
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

void run_trivial_move_setting(run *r, struct sorted_table_entry *ste){
	st_array *sa=r->st_body;
	st_array_set_now_PBA(sa, ste->PBA, TRIVIAL_MOVE_PBA);
}

void run_trivial_move_insert(run *r, uint32_t lba, uint32_t psa, bool last){
	st_array_insert_pair(r->st_body, lba, psa, true);
	r->now_entry_num++;
	if(last){
		__run_write_meta(r, r->st_body->bm->segment_manager, true);
	}
}

void run_copy_unlinked_flag_update(run *r, uint32_t ste_num, bool flag, uint32_t original_level, uint32_t original_recency){
	r->st_body->sp_meta[ste_num].unlinked_data_copy=flag;
	r->st_body->sp_meta[ste_num].original_level=original_level;
	r->st_body->sp_meta[ste_num].original_recency=original_recency;
}

void run_insert_done(run *r, bool merge_insert){
	if(r->pp && r->pp->buffered_num!=0){
		__run_write_buffer(r, r->st_body->bm->segment_manager,true, merge_insert?COMPACTIONDATAW:DATAW, NULL);
	}
	__run_write_meta(r, r->st_body->bm->segment_manager, true);

	if(r->pp){
		pp_free(r->pp);
	}
	r->pp=NULL;

	uint64_t mf_memory_usage=run_memory_usage(r, r->lsm->param.target_bit);
	uint32_t map_type=r->type==RUN_LOG?r->run_log_mf->type:r->st_body->param.map_type;
	__lsm_calculate_memory_usage(r->lsm,r->now_entry_num, mf_memory_usage, map_type, r->type==RUN_PINNING);
	/*
	if(r->type!=RUN_LOG){
		uint32_t prev_end=0;
		for (uint32_t i = 0; i < r->st_body->now_STE_num; i++){
			if(i==0){
				prev_end=r->st_body->sp_meta[i].end_lba;
			}
			else{
				if(r->st_body->sp_meta[i].start_lba < prev_end){
					EPRINT("sorting error!", true);
				}
				else{
					prev_end=r->st_body->sp_meta[i].end_lba;
				}
			}
		}
	}
	*/
}
#ifdef SC_MEM_OPT

typedef struct reinsert_node{
	bool prev_same;
	bool done;
	uint32_t lba;
	uint32_t piece_ppa;
	run *r;
	uint32_t ste;
	value_set *value;
	map_function *mf;
	map_read_param *param;
	fdriver_lock_t lock;
}reinsert_node;

static void *__read_for_piece_ppa_end_req(algo_req *req){
	reinsert_node *node=(reinsert_node*)req->param;
	fdriver_unlock(&node->lock);
	free(req);
	return NULL;
}
void __read_for_piece_ppa(lsmtree *lsm, uint32_t ppa, reinsert_node *issue_node){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	issue_node->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	fdriver_mutex_init(&issue_node->lock);
	fdriver_lock(&issue_node->lock);

	req->type=COMPACTIONDATAR;
	req->value=issue_node->value;
	req->end_req=__read_for_piece_ppa_end_req;
	req->param=(void*)issue_node;
	req->parents=NULL;
	issue_node->param->oob_set=(uint32_t*)lsm->bm->segment_manager->get_oob(lsm->bm->segment_manager, ppa);
	g_li->read(ppa, PAGESIZE, req->value, req);
}

uint32_t run_reinsert2(lsmtree *lsm, run *r, uint32_t start_lba, uint32_t data_num, struct shortcut_master *sc){
	uint32_t info_idx=r->info->idx;
	sc_dir_dp *temp_dp=sc_dir_dp_init(sc, start_lba);
	std::list<reinsert_node*> temp_list;
	reinsert_node *prev_node=NULL;
	reinsert_node *temp_node;

	static uint32_t reinsert_cnt=0;
	bool debug_flag=false;
	printf("reinsert_cnt :%u\n", reinsert_cnt++);

	for(uint32_t i=start_lba; i<start_lba+SC_PER_DIR; i++){
		uint32_t now_info_idx=sc_dir_dp_get_sc(temp_dp, sc, i);
		if(info_idx!=now_info_idx && now_info_idx!=NOT_ASSIGNED_SC){
			temp_node = (reinsert_node *)malloc(sizeof(reinsert_node));
			temp_node->lba = i;
			temp_node->piece_ppa = UINT32_MAX;
			temp_node->r = sc->info_set[now_info_idx].r;

			temp_node->ste = st_array_get_target_STE(temp_node->r->st_body, i);
			temp_node->prev_same = false;
			temp_node->value = NULL;
			//read target;
			if (prev_node)
			{
				if (temp_node->r == prev_node->r && temp_node->ste == prev_node->ste)
				{
					temp_node->prev_same = true;
				}
			}
			prev_node = temp_node;

			if (!temp_node->prev_same)
			{
				__read_for_piece_ppa(lsm, temp_node->r->st_body->sp_meta[temp_node->ste].piece_ppa, temp_node);
			}
			temp_list.push_back(temp_node);
		}
	}

	st_array_set_unaligned_block_write(r->st_body);

	std::vector<uint32_t> bulk_lba_set;
	uint32_t res=0;
	std::list<reinsert_node*>::iterator iter;
	value_set *prev_value_set=NULL;
	for(iter=temp_list.begin(); iter!=temp_list.end();){
		temp_node=*iter;
		uint32_t offset;
		if(temp_node->prev_same){
			offset=sp_find_offset_by_value(prev_value_set->value, temp_node->lba);
		}	
		else{
			if(prev_value_set){
				inf_free_valueset(prev_value_set, FS_MALLOC_R);
			}
			fdriver_lock(&temp_node->lock);
			fdriver_destroy(&temp_node->lock);
			offset=sp_find_offset_by_value(temp_node->value->value, temp_node->lba);
			prev_value_set=temp_node->value;
		}
		bulk_lba_set.push_back(temp_node->lba);
		temp_node->piece_ppa=run_translate_intra_offset(temp_node->r, temp_node->ste, offset);

		if(temp_node->r->type==RUN_PINNING){
			st_array_unlink_bit_set(temp_node->r->st_body, temp_node->ste, offset, temp_node->piece_ppa);
		}
	//	printf("global intra:%u\n", r->st_body->global_write_pointer);
		uint32_t intra_offset=r->st_body->global_write_pointer;
		if(r->type==RUN_LOG){
			uint32_t res=r->run_log_mf->insert(r->run_log_mf,  temp_node->lba, intra_offset);
			if(res!=INSERT_SUCCESS){
				std::map<uint32_t, uint32_t> *temp_tree=(std::map<uint32_t,uint32_t> *)r->run_log_mf->private_data;
				std::map<uint32_t, uint32_t>::iterator iter;
				for(iter=temp_tree->begin(); iter!=temp_tree->end(); iter++){
					printf("%u,%u\n", iter->first, iter->second);
				}
				EPRINT("it cannot be!!", true);
			}
		}
		else{
			EPRINT("error type in reinsert", true);
		}

		L2PBm_block_mixed_check_and_set(lsm->bm, temp_node->piece_ppa/L2PGAP/_PPB*_PPB, r->st_body->sid);
		st_array_insert_pair_for_reinsert(r->st_body, temp_node->lba, temp_node->piece_ppa, false);
		r->now_entry_num++;
		
		if(r->st_body->summary_write_alert){
			__run_write_meta(r, r->st_body->bm->segment_manager, false);
		}

		free(temp_node);
		temp_list.erase(iter++);
	}
	
	if(prev_value_set){
		inf_free_valueset(prev_value_set, FS_MALLOC_R);
	}

	res=bulk_lba_set.size();
	
	if(res!=0){
		shortcut_link_bulk_lba(sc, r, &bulk_lba_set, true);
	}
	sc_dir_dp_free(temp_dp);
	return res;
}

uint32_t run_reinsert(lsmtree *lsm, run *r, uint32_t start_lba, uint32_t data_num, struct shortcut_master *sc){
	uint32_t info_idx=r->info->idx;
	sc_dir_dp *temp_dp=sc_dir_dp_init(sc, start_lba);
	std::list<reinsert_node*> temp_list;
	reinsert_node *prev_node=NULL;
	reinsert_node *temp_node;

	static uint32_t reinsert_cnt=0;
	bool debug_flag=false;
	printf("reinsert_cnt :%u\n", reinsert_cnt++);

	for(uint32_t i=start_lba; i<start_lba+SC_PER_DIR; i++){
		uint32_t now_info_idx=sc_dir_dp_get_sc(temp_dp, sc, i);
		if(info_idx!=now_info_idx && now_info_idx!=NOT_ASSIGNED_SC){
			temp_node = (reinsert_node *)calloc(1, sizeof(reinsert_node));
			temp_node->lba = i;
			temp_node->piece_ppa = UINT32_MAX;
			temp_node->r = sc->info_set[now_info_idx].r;

			temp_node->ste = st_array_get_target_STE(temp_node->r->st_body, i);
			temp_node->prev_same = false;
			temp_node->value = NULL;

			temp_node->mf=temp_node->r->st_body->pba_array[temp_node->ste].mf;

			uint32_t intra_offset;
			intra_offset=temp_node->mf->query(temp_node->mf, i, &temp_node->param);
retry:
			
			temp_node->piece_ppa=run_translate_intra_offset(temp_node->r, temp_node->ste, intra_offset);
			temp_node->param->intra_offset=temp_node->piece_ppa%L2PGAP;
			if(temp_node->piece_ppa==UNLINKED_PSA){
				intra_offset=temp_node->mf->query_retry(temp_node->mf, temp_node->param);
				goto retry;
			}

			__read_for_piece_ppa(lsm, temp_node->piece_ppa/L2PGAP, temp_node);
			temp_list.push_back(temp_node);
		}
	}

	std::list<reinsert_node*>::iterator iter;
	uint32_t done_cnt;
	while(1){
		done_cnt = 0;
		for (iter = temp_list.begin(); iter != temp_list.end(); ++iter)
		{
			temp_node = *iter;
			if (temp_node->done)
			{
				done_cnt++;
				continue;
			}
			fdriver_lock(&temp_node->lock);
			fdriver_destroy(&temp_node->lock);

			uint32_t temp_intra_offset = temp_node->mf->oob_check(temp_node->mf, temp_node->param);
			if (temp_intra_offset == NOT_FOUND)
			{
retry2:
				temp_intra_offset=temp_node->mf->query_retry(temp_node->mf, temp_node->param);
				temp_node->piece_ppa=run_translate_intra_offset(temp_node->r, temp_node->ste, temp_intra_offset);
				temp_node->param->intra_offset=temp_node->piece_ppa%L2PGAP;
				if(temp_node->piece_ppa==UNLINKED_PSA){
					goto retry2;
				}
				__read_for_piece_ppa(lsm,temp_node->piece_ppa/L2PGAP, temp_node);
				continue;
			}
			else
			{
				temp_node->piece_ppa=temp_node->piece_ppa/L2PGAP*L2PGAP+temp_intra_offset;
				temp_node->done=true;
				temp_node->mf->query_done(temp_node->mf, temp_node->param);
				done_cnt++;
				continue;
			}
		}
		if(done_cnt==temp_list.size()){
			break;
		}
	}

	uint32_t res=temp_list.size();
	value_set *prev_value_set=NULL;
	for(iter=temp_list.begin(); iter!=temp_list.end();){
		temp_node=*iter;
		uint32_t offset;
		run_insert(r, temp_node->lba, UINT32_MAX, &temp_node->value->value[(temp_node->piece_ppa%L2PGAP)*LPAGESIZE], TEST_IO, lsm->shortcut, NULL);

		inf_free_valueset(temp_node->value, FS_MALLOC_R);

		free(temp_node);
		temp_list.erase(iter++);
	}

	sc_dir_dp_free(temp_dp);
	return res;
}
#endif