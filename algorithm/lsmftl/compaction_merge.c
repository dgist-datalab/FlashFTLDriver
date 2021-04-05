#include "compaction.h"
#include "version.h"
#include "lsmtree.h"
#include "page_manager.h"
#include "io.h"
#include <queue>
#include <list>
#include <stdlib.h>
extern compaction_master *_cm;
extern lsmtree LSM;
extern uint32_t debug_lba;
void *merge_end_req(algo_req *);
void read_map_param_init(read_issue_arg *read_arg, map_range *mr){
	inter_read_alreq_param *param;
	for(int i=read_arg->from; i<=read_arg->to; i++){
		//param=compaction_get_read_param(_cm);
		param=(inter_read_alreq_param*)calloc(1,sizeof(inter_read_alreq_param));
		param->map_target=&mr[i];
		mr[i].data=NULL;
		param->data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
		fdriver_lock_init(&param->done_lock, 0);
		read_arg->param[i]=param;
	}
}

bool read_map_done_check(inter_read_alreq_param *param, bool check_page_sst){
	if(check_page_sst){
		param->map_target->data=param->data->value;
	}
	fdriver_lock(&param->done_lock);
	return true;
}

static bool map_done(inter_read_alreq_param *param){
	param->map_target->data=NULL;
	inf_free_valueset(param->data, FS_MALLOC_R);
	fdriver_destroy(&param->done_lock);
	invalidate_map_ppa(LSM.pm->bm, param->map_target->ppa, true);
	free(param);
	//compaction_free_read_param(_cm, param);
	return true;
}

static inline uint32_t coherence_sst_kp_pair_num(run *r, uint32_t start_idx, uint32_t *end_idx, uint32_t *map_num){
	uint32_t cnt=0;
	uint32_t now_map_num=0;
	sst_file *prev_sptr=NULL;
	sst_file *sptr;
	uint32_t iter_start_idx=start_idx;
	for(; iter_start_idx<r->now_sst_file_num; iter_start_idx++){
		sptr=&r->sst_set[iter_start_idx];
		if(!prev_sptr){
			prev_sptr=sptr;
			prev_sptr=&r->sst_set[iter_start_idx];
			now_map_num+=prev_sptr->map_num;
			continue;
		}
		if(sst_range_overlap(prev_sptr, sptr)){
			prev_sptr=sptr;
			prev_sptr=&r->sst_set[iter_start_idx];
			now_map_num+=prev_sptr->map_num;
			cnt++;
		}
		else break;
	}

	*end_idx=start_idx+cnt;
	*map_num=now_map_num;

	return ((*map_num)*L2PGAP+(*map_num)*KP_IN_PAGE);
}

static map_range * make_mr_set(sst_file *set, uint32_t start_idx, uint32_t end_idx, uint32_t map_num){
	if(!map_num) return NULL;
	map_range *mr=(map_range*)calloc(map_num,sizeof(map_range));
	map_range *mptr;
	uint32_t idx=0;
	uint32_t mr_idx=0;
	for(uint32_t i=start_idx; i<=end_idx; i++){
		for_each_map_range(&set[i], mptr, idx){
			mr[mr_idx++]=*mptr;
		}
	}
	return mr;
}

static void  bulk_invalidation(run *r, uint32_t* border_idx, uint32_t border_lba){
	uint32_t i=0;

	for(i=(*border_idx); i<r->now_sst_file_num; i++){
		sst_file *sptr=&r->sst_set[i];
		if(sptr->end_lba<=border_lba){
			for(uint32_t j=sptr->file_addr.piece_ppa; j<(sptr->end_ppa-sptr->map_num)*2; j++){
				invalidate_piece_ppa(LSM.pm->bm, j, false);
			}
			/* invalidate in map_done
			for(uint32_t j=sptr->end_ppa-sptr->map_num; j<sptr->end_ppa; j++){
				invalidate_map_ppa(LSM.pm->bm, j, true);
			}*/
		}
		else{
			break;
		}
	}
	(*border_idx)=i;
}

static uint32_t issue_read_kv_for_bos_merge(sst_bf_out_stream *bos, std::queue<key_ptr_pair> *kpq,
		bool round_final, uint32_t newer_version, uint32_t older_version){
	key_value_wrapper *read_target;
	uint32_t res=0;

	while(kpq->size()){
		key_ptr_pair target_pair=kpq->front();
		key_value_wrapper *kv_wrapper=(key_value_wrapper*)calloc(1,sizeof(key_value_wrapper));

		kv_wrapper->piece_ppa=target_pair.piece_ppa;
		kv_wrapper->kv_ptr.lba=target_pair.lba;

		if(version_is_early_invalidate(LSM.last_run_version, newer_version) || 
				version_is_early_invalidate(LSM.last_run_version, older_version)){
			invalidate_piece_ppa(LSM.pm->bm, kv_wrapper->piece_ppa, false);
		}
		else{
			invalidate_piece_ppa(LSM.pm->bm, kv_wrapper->piece_ppa, true);
		}
		/*version alread check in stream sorting logci*/

		if((read_target=sst_bos_add(bos, kv_wrapper, _cm))){
			if(!read_target->param){
				EPRINT("can't be",true);
			}
			algo_req *read_req=(algo_req*)malloc(sizeof(algo_req));
			read_req->type=COMPACTIONDATAR;
			read_req->param=(void*)read_target;
			read_req->end_req=merge_end_req;
			io_manager_issue_read(PIECETOPPA(read_target->piece_ppa),
					read_target->param->data, read_req, false);
		}

		kpq->pop();
		res++;
	}

	if(kpq->empty() && round_final){
		if((read_target=sst_bos_get_pending(bos, _cm))){
			algo_req *read_req=(algo_req*)malloc(sizeof(algo_req));
			read_req->type=COMPACTIONDATAR;
			read_req->param=(void*)read_target;
			read_req->end_req=merge_end_req;		
			io_manager_issue_read(PIECETOPPA(read_target->piece_ppa),
					read_target->param->data, read_req, false);
		}
	}
	return res;
}

static bool merge_invalidation_function(level *des, uint32_t stream_id, uint32_t version, key_ptr_pair kp){
	if(stream_id==UINT32_MAX){
		invalidate_piece_ppa(LSM.pm->bm, kp.piece_ppa, true);
		return false;
	}
	else{
		uint32_t a, b;
		a=version_map_lba(LSM.last_run_version, kp.lba);
		b=version;
		if(version_compare(LSM.last_run_version, a, b) > 0){
			if(version_is_early_invalidate(LSM.last_run_version, b)){
				invalidate_piece_ppa(LSM.pm->bm, kp.piece_ppa, false);
			}
			else{
				invalidate_piece_ppa(LSM.pm->bm, kp.piece_ppa, true);
			}
			return false;
		}
	}
	return true;
}


typedef struct mr_free_set{
	map_range *mr_set;
	run *r;
	uint32_t start_idx;
	uint32_t end_idx;
	uint32_t map_num;
}mr_free_set;

void map_range_postprocessing(std::list<mr_free_set>* mr_list,  uint32_t bound_lba, bool last){
	std::list<mr_free_set>::iterator mr_iter=mr_list->begin();
	for(;mr_iter!=mr_list->end(); ){
		mr_free_set now=*mr_iter;
		if(last || now.mr_set[now.map_num].end_lba <= bound_lba){
			for(uint32_t i=now.start_idx; i<=now.end_idx; i++){
				lsmtree_gc_unavailable_unset(&LSM, &now.r->sst_set[i], UINT32_MAX);
			}
			free(now.mr_set);
			mr_list->erase(mr_iter++);
		}
		else{
			break;
		}
	}
}


level* compaction_merge(compaction_master *cm, level *des, uint32_t *idx_set){
	_cm=cm;

	version_get_merge_target(LSM.last_run_version, idx_set);
	run *new_run=run_init(des->max_sst_num/des->max_run_num, UINT32_MAX, 0);

	LSM.now_merging_run[0]=idx_set[0];
	LSM.now_merging_run[1]=idx_set[1];

	run *older=&des->array[idx_set[0]];
	run	*newer=&des->array[idx_set[1]];

	LSM.monitor.compaction_cnt[des->idx+1]++;

	for(uint32_t i=0; i<2; i++){
		version_unpopulate_run(LSM.last_run_version, idx_set[i]);
	}

	if(LSM.monitor.compaction_cnt[des->idx+1]==49){
		LSM.global_debug_flag=true;
	}

	read_issue_arg read_arg1={0,}, read_arg2={0,};
	read_arg_container thread_arg;
	thread_arg.end_req=merge_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*2);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=2;

	uint32_t newer_sst_idx=0, newer_sst_idx_end;
	uint32_t older_sst_idx=0, older_sst_idx_end;
	uint32_t now_newer_map_num=0, now_older_map_num=0;
	uint32_t newer_borderline=0;
	uint32_t older_borderline=0;
	uint32_t border_lba;

	uint32_t target_ridx=version_get_empty_ridx(LSM.last_run_version);
	sst_pf_out_stream *os_set[2];

	sst_bf_out_stream *bos=NULL;
	sst_bf_in_stream *bis=NULL;

	bool init=true;
	uint32_t max_target_piece_num;
	std::queue<key_ptr_pair> *kpq=new std::queue<key_ptr_pair>();
	std::list<mr_free_set> *new_range_set=new std::list<mr_free_set>();
	std::list<mr_free_set> *old_range_set=new std::list<mr_free_set>();
	while(!(older_sst_idx==older->now_sst_file_num && 
				newer_sst_idx==newer->now_sst_file_num)){
		now_newer_map_num=now_older_map_num=0;
		max_target_piece_num=0;
		max_target_piece_num+=
			newer_sst_idx<newer->now_sst_file_num?coherence_sst_kp_pair_num(newer,newer_sst_idx, &newer_sst_idx_end, &now_newer_map_num):0;
		max_target_piece_num+=
			older_sst_idx<older->now_sst_file_num?coherence_sst_kp_pair_num(older,older_sst_idx, &older_sst_idx_end, &now_older_map_num):0;


		if(bis){
			max_target_piece_num+=(bis->map_data->size()+1+1)*L2PGAP; // buffered + additional mapping
		}

		if(bos){
			max_target_piece_num+=bos->kv_wrapper_q->size()+L2PGAP;
		}

		uint32_t needed_seg_num=(max_target_piece_num/L2PGAP+1)/_PPS+1+1; //1 for front fragment, 1 for behid fragment
		if(page_manager_get_total_remain_page(LSM.pm, false) < needed_seg_num*_PPS){
	//		__do_gc(LSM.pm, false, max_target_piece_num/L2PGAP+(max_target_piece_num%L2PGAP?1:0));
			__do_gc(LSM.pm, false, needed_seg_num*_PPS);

			/*find */
			if(newer->sst_set[newer_sst_idx].trimed_sst_file){
				newer_sst_idx++;
			}
			if(older->sst_set[older_sst_idx].trimed_sst_file){
				older_sst_idx++;
			}
			continue;
		}

		map_range *newer_mr=make_mr_set(newer->sst_set, newer_sst_idx, newer_sst_idx_end, now_newer_map_num);
		map_range *older_mr=make_mr_set(older->sst_set, older_sst_idx, older_sst_idx_end, now_older_map_num);


		if(newer_mr){
			mr_free_set temp_mr_free_set={newer_mr, newer, newer_sst_idx, newer_sst_idx_end, now_newer_map_num-1};
			for(uint32_t i=newer_sst_idx; i<=newer_sst_idx_end; i++){
				lsmtree_gc_unavailable_set(&LSM, &newer->sst_set[i], UINT32_MAX);
			}
			new_range_set->push_back(temp_mr_free_set);
		}

		if(older_mr){
			mr_free_set temp_mr_free_set={older_mr, older, older_sst_idx, older_sst_idx_end, now_older_map_num-1};
			for(uint32_t i=older_sst_idx; i<=older_sst_idx_end; i++){
				lsmtree_gc_unavailable_set(&LSM, &older->sst_set[i], UINT32_MAX);
			}
			old_range_set->push_back(temp_mr_free_set);
		}

		bool last_round_check=((newer_sst_idx_end+1==newer->now_sst_file_num) && (older_sst_idx_end+1==older->now_sst_file_num));
		uint32_t total_map_num=now_newer_map_num+now_older_map_num;
		uint32_t read_done=0;
		uint32_t older_prev=0, newer_prev=0;
		read_arg1={0,}; read_arg2={0,};
		while(read_done!=total_map_num){
//			if(LSM.global_debug_flag){
//				printf("merge cnt:%u round info - o_idx:%u n_idx%u read_done:%u\n", LSM.monitor.compaction_cnt[des->idx+1],older_sst_idx, newer_sst_idx, read_done);
//			}
			uint32_t shard=LOWQDEPTH/2;

			if(newer_mr){
				read_arg1.from=newer_prev;
				read_arg1.to=MIN(newer_prev+shard, now_newer_map_num-1);
				if(TARGETREADNUM(read_arg1)){
					read_map_param_init(&read_arg1, newer_mr);
				}
			}
			else{
				read_arg1.from=1;
				read_arg1.to=0;
			}
			
			if(older_mr){
				read_arg2.from=older_prev;
				read_arg2.to=MIN(older_prev+shard, now_older_map_num-1);
				if(TARGETREADNUM(read_arg2)){
					read_map_param_init(&read_arg2, older_mr);
				}
			}
			else{
				read_arg2.from=1;
				read_arg2.to=0;
			}

			//pos setting
			if(init){
				init=false;
				os_set[0]=sst_pos_init_mr(&newer_mr[read_arg1.from], read_arg1.param,
						TARGETREADNUM(read_arg1), read_map_done_check, map_done);
				os_set[0]->version_idx=idx_set[1];
				os_set[1]=sst_pos_init_mr(&older_mr[read_arg2.from], read_arg2.param,
						TARGETREADNUM(read_arg2), read_map_done_check, map_done);
				os_set[1]->version_idx=idx_set[0];
			}
			else{
				if(newer_mr){
					sst_pos_add_mr(os_set[0], &newer_mr[read_arg1.from], read_arg1.param,
							TARGETREADNUM(read_arg1));
				}
				if(older_mr){
					sst_pos_add_mr(os_set[1], &older_mr[read_arg2.from], read_arg2.param,
							TARGETREADNUM(read_arg2));
				}
			}

			thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);//read work
			
			if(newer_mr && older_mr){
				border_lba=MIN(newer_mr[read_arg1.to].end_lba, 
					older_mr[read_arg2.to].end_lba);
			}
			else{
				border_lba=(newer_mr?newer_mr[read_arg1.to].end_lba:
						older_mr[read_arg2.to].end_lba);
			}

		/*
			if((newer_mr && newer->sst_set[newer_sst_idx].map_num==1 && !older_mr) || 
					(older_mr && older->sst_set[older_sst_idx].map_num==1 && !newer_mr)){
				EPRINT("debug point", false);
			}*/

			if( (os_set[0]->now_file_empty && os_set[0]->now_mr==os_set[0]->mr_set->front()) || 
				(os_set[1]->now_file_empty && os_set[1]->now_mr==os_set[1]->mr_set->front())){
				EPRINT("debug point", false);
			}

			//sorting
			LSM.monitor.merge_valid_entry_cnt+=stream_sorting(NULL, 2, os_set, NULL, kpq, 
				last_round_check,
				border_lba,/*limit*/
				target_ridx, 
				true,
				merge_invalidation_function);

			read_done+=TARGETREADNUM(read_arg1)+TARGETREADNUM(read_arg2);
			if(newer_mr){
				newer_prev=read_arg1.to+1;
			}
			if(older_mr){
				older_prev=read_arg2.to+1;
			}
		}

		if(bos==NULL){
			bos=sst_bos_init(read_map_done_check, true);
		}
		if(bis==NULL){
			bis=tiering_new_bis();
		}

		uint32_t entry_num=issue_read_kv_for_bos_merge(bos, kpq, last_round_check, idx_set[1], idx_set[0]);
		border_lba=issue_write_kv_for_bis(&bis, bos, new_run, entry_num, 
				target_ridx, last_round_check);

		/*check end*/
		bulk_invalidation(newer, &newer_borderline, border_lba);
		bulk_invalidation(older, &older_borderline, border_lba);

		map_range_postprocessing(new_range_set, border_lba, last_round_check);
		map_range_postprocessing(old_range_set, border_lba, last_round_check);

		newer_sst_idx=newer_sst_idx_end+1;
		older_sst_idx=older_sst_idx_end+1;
	
	//	EPRINT("should delete before run", false);

	//	EPRINT("should delete before run", false);
		//return NULL;
	}

	sst_file *last_file;
	if((last_file=bis_to_sst_file(bis))){
		lsmtree_gc_unavailable_set(&LSM, last_file, UINT32_MAX);
		run_append_sstfile_move_originality(new_run, last_file);
		sst_free(last_file, LSM.pm);
	}

	if(bis->seg->used_page_num!=_PPS){
		if(LSM.pm->temp_data_segment){
			EPRINT("should be NULL", true);
		}
		LSM.pm->temp_data_segment=bis->seg;
	}

	sst_bis_free(bis);
	sst_bos_free(bos, _cm);

	LSM.monitor.merge_total_entry_cnt+=os_set[0]->total_poped_num+
		os_set[1]->total_poped_num;

	sst_pos_free(os_set[0]);
	sst_pos_free(os_set[1]);
	delete kpq;
	free(thread_arg.arg_set);

	level *res=level_init(des->max_sst_num, des->max_run_num, des->istier, des->idx);
	//level_run_reinit(des, idx_set[1]);

	run *rptr; uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(ridx!=idx_set[0] && ridx!=idx_set[1]){
			if(rptr->now_sst_file_num){
				level_append_run_copy_move_originality(res, rptr, ridx);
			}
		}
	}

	level_update_run_at_move_originality(res, target_ridx, new_run, true);
	version_populate_run(LSM.last_run_version, target_ridx);

	sst_file *temp_sptr;
	uint32_t temp_sidx;
	for_each_sst(new_run, temp_sptr, temp_sidx){
		lsmtree_gc_unavailable_unset(&LSM, temp_sptr, UINT32_MAX);
	}
	run_free(new_run);
//	level_print(res);
//	level_contents_print(res, true);
//	lsmtree_gc_unavailable_sanity_check(&LSM);
	version_poped_update(LSM.last_run_version);
	version_reinit_early_invalidation(LSM.last_run_version, 2, idx_set);

	printf("merge %u,%u to %u\n", idx_set[0], idx_set[1], idx_set[0]);
	delete new_range_set;
	delete old_range_set;

	LSM.now_merging_run[0]=UINT32_MAX;
	LSM.now_merging_run[1]=UINT32_MAX;
	return res;
}

void *merge_end_req(algo_req *req){
	inter_read_alreq_param *r_param;
	key_value_wrapper *kv_wrapper;
	switch(req->type){
		case MAPPINGR:
			r_param=(inter_read_alreq_param*)req->param;
			fdriver_unlock(&r_param->done_lock);
			break;
		case COMPACTIONDATAR:
			kv_wrapper=(key_value_wrapper*)req->param;

			fdriver_unlock(&kv_wrapper->param->done_lock);
			break;

	}
	free(req);
	return NULL;
}
