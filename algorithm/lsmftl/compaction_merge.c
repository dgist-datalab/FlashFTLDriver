#include "compaction.h"
#include "version.h"
#include "lsmtree.h"
#include "page_manager.h"
#include "io.h"
#include <queue>
#include <list>
#include <stdlib.h>
#include <map>
extern compaction_master *_cm;
extern lsmtree LSM;
extern uint32_t debug_lba;
extern uint32_t debug_piece_ppa;
void *merge_end_req(algo_req *);
void read_map_param_init(read_issue_arg *read_arg, map_range *mr){
	inter_read_alreq_param *param;
	uint32_t param_idx=0;
	for(int i=read_arg->from; i<=read_arg->to; i++){
		//param=compaction_get_read_param(_cm);
		if(LSM.pm->bm->is_invalid_piece(LSM.pm->bm, mr[i].ppa*L2PGAP)){
			printf("ppa:%u piece_ppa:%u break!\n", mr[i].ppa, mr[i].ppa*L2PGAP);
		}
		param=(inter_read_alreq_param*)calloc(1, sizeof(inter_read_alreq_param));
		param->map_target=&mr[i];
		mr[i].data=NULL;
		mr[i].read_done=false;
		mr[i].isgced=false;
		param->data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
		fdriver_lock_init(&param->done_lock, 0);
		read_arg->param[param_idx++]=param;
	}

	read_arg->map_target_for_gc=mr;
}

bool read_map_done_check(inter_read_alreq_param *param, bool check_page_sst){
	if(check_page_sst && param->map_target->read_done) return true;

	if(check_page_sst){
		param->map_target->data=param->data->value;
		param->map_target->read_done=true;
	}
	
	fdriver_lock(&param->done_lock);
	return true;
}

static bool invalid_map_done(inter_read_alreq_param *param, bool inv_flag){
	param->map_target->data=NULL;
	inf_free_valueset(param->data, FS_MALLOC_R);
	fdriver_destroy(&param->done_lock);
	if(!param->map_target->isgced){
		invalidate_map_ppa(LSM.pm->bm, param->map_target->ppa, inv_flag);
	}
	param->map_target->isgced=false;
	free(param);
	//compaction_free_read_param(_cm, param);
	return true;
}

static bool not_invalid_map_done(inter_read_alreq_param *param, bool inv_flag){
	param->map_target->data=NULL;
	inf_free_valueset(param->data, FS_MALLOC_R);
	fdriver_destroy(&param->done_lock);
	free(param);
	return true;
}

static inline uint32_t coherence_sst_kp_pair_num(run *r, uint32_t start_idx, uint32_t *end_idx, uint32_t *map_num){
	uint32_t cnt=0;
	uint32_t now_map_num=0;
	sst_file *prev_sptr=NULL;
	sst_file *sptr;
	uint32_t iter_start_idx=start_idx;
	for(; iter_start_idx<r->now_sst_num; iter_start_idx++){
		sptr=&r->sst_set[iter_start_idx];
		if(!prev_sptr){
			prev_sptr=sptr;
			prev_sptr=&r->sst_set[iter_start_idx];
			now_map_num+=prev_sptr->map_num;
			continue;
		}
		if(sst_physical_range_overlap(prev_sptr, sptr)){
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
		if(set[i].type==BLOCK_FILE){
			for_each_map_range(&set[i], mptr, idx){
				mr[mr_idx]=*mptr;
				mr[mr_idx].data=NULL;
				mr_idx++;
			}
		}
		else{
			mr[mr_idx].start_lba=set[i].start_lba;
			mr[mr_idx].end_lba=set[i].end_lba;
			mr[mr_idx].ppa=set[i].file_addr.map_ppa;
			mr[mr_idx].data=NULL;
			mr_idx++;
		}
	}
	return mr;
}

static map_range * make_mr_set_for_gc(sst_file *set, uint32_t start_idx, uint32_t end_idx,
		uint32_t border_lba, uint32_t* map_num){
	uint32_t target_map_num=0;
	sst_file *sptr=&set[start_idx];
	uint32_t first_map_idx=0;
	if(sptr->type==BLOCK_FILE){
		first_map_idx=sst_upper_bound_map_idx(sptr, border_lba);
		if(sptr->block_file_map[first_map_idx].end_lba==border_lba){
			first_map_idx++;
		}
		target_map_num=sptr->map_num-first_map_idx;
	}
	else{
		target_map_num=1;
	}

	map_range *res=NULL;
	for(uint32_t i=start_idx+1; i<=end_idx; i++){
		sptr=&set[i];
		if(set[i].type==BLOCK_FILE){
			target_map_num+=sptr->map_num;
		}
		else{
			target_map_num++;
		}
	}

	if(!target_map_num) {
		*map_num=0;
		return NULL;
	}
	res=(map_range*)calloc(target_map_num, sizeof(map_range));

	uint32_t mr_idx=0;
	for(uint32_t i=start_idx; i<=end_idx; i++){
		sptr=&set[i];
		if(sptr->type==BLOCK_FILE){
			if(first_map_idx && i==start_idx){
				for(uint32_t j=first_map_idx; j<sptr->map_num; j++){
					res[mr_idx]=sptr->block_file_map[j];
					res[mr_idx].data=NULL;	
					mr_idx++;
				}
				continue;
			}
			map_range *mptr;
			uint32_t idx;
			for_each_map_range(sptr, mptr, idx){
				res[mr_idx]=*mptr;
				res[mr_idx].data=NULL;
				mr_idx++;
			}
		}
		else{
			res[mr_idx].start_lba=sptr->start_lba;
			res[mr_idx].end_lba=sptr->end_lba;
			res[mr_idx].ppa=sptr->file_addr.map_ppa;
			res[mr_idx].data=NULL;
			mr_idx++;
		}
	}

	*map_num=target_map_num;
	return res;
}

void compaction_adjust_by_gc(read_issue_arg *read_arg_set, sst_pf_out_stream *pos, uint32_t last_lba, 
		run *rptr, map_range **mr, uint32_t sst_file_type, bool force){
#ifdef UPDATING_COMPACTION_DATA
	if(!force){
		if(!rptr->update_by_gc) return;
	}

	if(sst_file_type==PAGE_FILE){
		EPRINT("not implemented", true);		
	}
	else{
		sst_pos_delay_free(pos, (void*)*mr);
	//	free(*mr);
		uint32_t closed_sidx=run_retrieve_upper_bound_sst_idx(rptr, last_lba);
		if(closed_sidx==rptr->now_sst_num-1 && rptr->sst_set[closed_sidx].end_lba==last_lba){
			read_arg_set->max_num=0;
	//		read_arg_set->mr=NULL;
			*mr=NULL;
			return;
		}
		uint32_t sst_file_num=rptr->now_sst_num-1;
		uint32_t map_num=0;
		
		*mr=make_mr_set_for_gc(rptr->sst_set, closed_sidx, sst_file_num,
				last_lba, &map_num);
		read_arg_set->max_num=map_num;
		read_arg_set->from=0;
		read_arg_set->to=read_arg_set->from-1;

		pos->gced_out_stream++;
	}
#else
	return;
#endif
}

static uint32_t tiering_invalidation_function(level *des, uint32_t stream_id, uint32_t version,
		key_ptr_pair kp, bool overlap, bool inplace){
	if(overlap){
		invalidate_kp_entry(kp.lba, kp.piece_ppa, version, true);
		return false;
	}
	else{
		uint32_t a, b;
		a=version_map_lba(LSM.last_run_version, kp.lba);
		b=version;
		if(inplace){
			if(version_compare(LSM.last_run_version, a, b) > 0){
				invalidate_kp_entry(kp.lba, kp.piece_ppa, version, true);
				return false;
			}
		}
		else{
			if(des==NULL //for compaction_merge
					|| (des->idx!=0 && !version_belong_level(LSM.last_run_version, a, des->idx-1))){
				if(version_compare(LSM.last_run_version, a, b) > 0){
					invalidate_kp_entry(kp.lba, kp.piece_ppa, version, true);
					return false;
				}
			}
		}
		return true;
	}
}

typedef struct mr_free_set{
	map_range *mr_set;
	run *r;
	uint32_t start_idx;
	uint32_t end_idx;
	uint32_t map_num;
}mr_free_set;

static inline mr_free_set making_MFS(read_issue_arg *arg, map_range *mr_set, run *rptr){
	mr_free_set res={mr_set, rptr, (uint32_t)arg->from, arg->to, arg->max_num-1};
	return res;
}

static inline void map_range_preprocessing(mr_free_set free_set, std::list<mr_free_set>* mr_list, bool is_sst_file){
#ifdef DEMAND_SEG_LOCK
#else
	run *rptr=free_set.r;
#endif
	uint32_t start_sst_idx=free_set.start_idx, end_idx=free_set.end_idx;
	for(uint32_t i=start_sst_idx; i<=end_idx; i++){

	//	printf("[%u] %u -> %u~%u\n", cnt++, i, free_set.mr_set[i].ppa, 
	//			free_set.mr_set[i].ppa/_PPS);
#ifdef DEMAND_SEG_LOCK
#else
		if(is_sst_file){
			lsmtree_gc_unavailable_set(&LSM, &rptr->sst_set[i], UINT32_MAX);
		}
		else{
			lsmtree_gc_unavailable_set(&LSM, NULL, free_set.mr_set[i].ppa/_PPS);
		}
#endif
	}
	mr_list->push_back(free_set);
}

static inline void map_range_postprocessing(std::list<mr_free_set>* mr_list,  uint32_t bound_lba, bool last, bool should_free, bool is_sst_file){
	std::list<mr_free_set>::iterator mr_iter=mr_list->begin();
	for(;mr_iter!=mr_list->end(); ){
		mr_free_set now=*mr_iter;
		if(last || now.mr_set[now.map_num].end_lba <= bound_lba){
			for(uint32_t i=now.start_idx; i<=now.end_idx; i++){
#ifdef DEMAND_SEG_LOCK
#else
				if(is_sst_file){
					lsmtree_gc_unavailable_unset(&LSM, &now.r->sst_set[i], UINT32_MAX);
				}
				else{
		
					//printf("free [%u] %u -> %u~%u\n", cnt++, i, now.mr_set[i].ppa, 
					//		now.mr_set[i].ppa/_PPS);
					lsmtree_gc_unavailable_unset(&LSM, NULL, now.mr_set[i].ppa/_PPS);
				}
#endif
			}
			if(should_free){
				free(now.mr_set);
			}
			mr_list->erase(mr_iter++);
		}
		else{
			break;
		}
	}
}

level *compaction_merge_empty_run(level *des, run *older, run *newer, uint32_t *idx_set){
	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx, des->max_contents_num, des->check_full_by_size);
	uint32_t big_order=MAX(
			version_to_order(LSM.last_run_version, des->idx, idx_set[0]),
			version_to_order(LSM.last_run_version, des->idx, idx_set[1]));
	uint32_t small_order=MIN(
			version_to_order(LSM.last_run_version, des->idx, idx_set[0]),
			version_to_order(LSM.last_run_version, des->idx, idx_set[1]));

	run *rptr; uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(ridx!=idx_set[0] && ridx!=idx_set[1]){
			if(rptr->now_sst_num || rptr->sst_num_zero_by_gc){
				level_append_run_copy_move_originality(res, rptr, ridx);
			}
		}
	}

	if(older->now_sst_num==0 &&  newer->now_sst_num==0){
		printf("double empty merging\n");
		version_clear_target(LSM.last_run_version, 
				version_order_to_version(LSM.last_run_version, des->idx, small_order), 
				des->idx);
		version_clear_target(LSM.last_run_version, 
				version_order_to_version(LSM.last_run_version, des->idx, big_order), 
				des->idx);
	}
	else if(older->now_sst_num==0){
		printf("big order merging\n");
		version_clear_target(LSM.last_run_version, version_order_to_version(LSM.last_run_version, des->idx, small_order), des->idx);
		level_update_run_at_move_originality(res, version_order_to_ridx(LSM.last_run_version, des->idx, big_order), newer, true);
	}
	else{
		printf("small order merging\n");
		version_clear_target(LSM.last_run_version, version_order_to_version(LSM.last_run_version, des->idx, big_order), des->idx);
		level_update_run_at_move_originality(res, version_order_to_ridx(LSM.last_run_version, des->idx, small_order), older, true);
	}
	return res;
}

level* compaction_merge(compaction_master *cm, level *des, uint32_t *idx_set){
	_cm=cm;

	LSM.now_merging_run[0]=idx_set[0];
	LSM.now_merging_run[1]=idx_set[1];

	uint32_t big_order=MAX(
			version_to_order(LSM.last_run_version, des->idx, idx_set[0]),
			version_to_order(LSM.last_run_version, des->idx, idx_set[1]));
	uint32_t small_order=MIN(
			version_to_order(LSM.last_run_version, des->idx, idx_set[0]),
			version_to_order(LSM.last_run_version, des->idx, idx_set[1]));

	run *older=&des->array[version_order_to_ridx(LSM.last_run_version, des->idx, small_order)];
	run *newer=&des->array[version_order_to_ridx(LSM.last_run_version, des->idx, big_order)];

	if(older->now_sst_num==0 || newer->now_sst_num==0){	
		return compaction_merge_empty_run(des, older, newer, idx_set);
	}

	run *new_run=run_init(des->max_sst_num/des->max_run_num, UINT32_MAX, 0);
	read_issue_arg read_arg1={0,}, read_arg2={0,};
	read_issue_arg read_arg1_prev={0,}, read_arg2_prev={0,};
	read_arg_container thread_arg;
	thread_arg.end_req=merge_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*MERGED_RUN_NUM);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=MERGED_RUN_NUM;

	LSM.read_arg_set=thread_arg.arg_set;
	LSM.now_compaction_stream_num=MERGED_RUN_NUM;
	read_arg1.page_file=false;
	read_arg2.page_file=false;
	static int cnt=0;
#ifdef LSM_DEBUG
	printf("merge cnt:%u\n", cnt++);
	if(cnt==35){
		printf("break!\n");
	}
#endif

	uint32_t newer_sst_idx=0, newer_sst_idx_end;
	uint32_t older_sst_idx=0, older_sst_idx_end;
	uint32_t now_newer_map_num=0, now_older_map_num=0;
	//uint32_t newer_borderline=0;
	//uint32_t older_borderline=0;

	uint32_t target_version=idx_set[0];
	uint32_t target_ridx=version_to_ridx(LSM.last_run_version,  des->idx, target_version);
	sst_pf_out_stream *os_set[MERGED_RUN_NUM]={0,};

	LSM.compactioning_pos_set=os_set;

	sst_bf_out_stream *bos=NULL;
	sst_bf_in_stream *bis=NULL;

	bool init=true;
	uint32_t max_target_piece_num;
	std::queue<key_ptr_pair> *kpq=new std::queue<key_ptr_pair>();
	std::queue<uint32_t> *locked_seg_q=new std::queue<uint32_t>();
	std::list<mr_free_set> *new_range_set=new std::list<mr_free_set>();
	std::list<mr_free_set> *old_range_set=new std::list<mr_free_set>();
	bool gced=false;
	uint32_t border_lba=UINT32_MAX;
	uint32_t stream_newer_border_lba=UINT32_MAX;
	uint32_t stream_older_border_lba=UINT32_MAX;
	while(!(older_sst_idx==older->now_sst_num && 
				newer_sst_idx==newer->now_sst_num)){

		now_newer_map_num=now_older_map_num=0;

		max_target_piece_num+=
			newer_sst_idx<newer->now_sst_num?coherence_sst_kp_pair_num(newer,newer_sst_idx, &newer_sst_idx_end, &now_newer_map_num):0;
		max_target_piece_num+=
			older_sst_idx<older->now_sst_num?coherence_sst_kp_pair_num(older,older_sst_idx, &older_sst_idx_end, &now_older_map_num):0;

		map_range *newer_mr=NULL, *older_mr=NULL;
		if(gced){
			if(newer->update_by_gc){
				newer_mr=make_mr_set_for_gc(newer->sst_set, newer_sst_idx, newer_sst_idx_end, 
						stream_newer_border_lba, &now_newer_map_num);
				newer->update_by_gc=false;
			}

			if(older->update_by_gc){
				older_mr=make_mr_set_for_gc(older->sst_set, older_sst_idx, older_sst_idx_end, 
						stream_older_border_lba, &now_older_map_num);
				older->update_by_gc=false;
			}
		}

		if(newer_mr==NULL){
			newer_mr=make_mr_set(newer->sst_set, newer_sst_idx, newer_sst_idx_end, now_newer_map_num);
		}

		if(older_mr==NULL){
			older_mr=make_mr_set(older->sst_set, older_sst_idx, older_sst_idx_end, now_older_map_num);
		}

		gced=false;

		if(newer_mr){
			mr_free_set temp_mr_free_set={newer_mr, newer, newer_sst_idx, newer_sst_idx_end, now_newer_map_num-1};
			map_range_preprocessing(temp_mr_free_set, new_range_set, true);
		}

		if(older_mr){
			mr_free_set temp_mr_free_set={older_mr, older, older_sst_idx, older_sst_idx_end, now_older_map_num-1};
			map_range_preprocessing(temp_mr_free_set, old_range_set, true);
		}

		bool last_round_check=((newer_sst_idx_end+1==newer->now_sst_num) && (older_sst_idx_end+1==older->now_sst_num));
		uint32_t total_map_num=now_newer_map_num+now_older_map_num;
		uint32_t read_done=0;
		uint32_t older_prev=0, newer_prev=0;
		while(read_done!=total_map_num){
			uint32_t shard=LOWQDEPTH/(1+1);

			if(newer_mr){
				read_arg1.from=newer_prev;
				read_arg1.to=MIN(newer_prev+shard, now_newer_map_num-1);
				if(TARGETREADNUM(read_arg1)){
					read_map_param_init(&read_arg1, newer_mr);
					read_arg1.version_for_gc=
						version_order_to_version(LSM.last_run_version,  des->idx, big_order);
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
					read_arg2.version_for_gc=
						version_order_to_version(LSM.last_run_version,  des->idx, small_order);
				}
			}
			else{
				read_arg2.from=1;
				read_arg2.to=0;
			}

//			printf("read_arg1:%u~%u, read_arg2:%u~%u\n", read_arg1.from, read_arg1.to, read_arg2.from, read_arg2.to);

			//pos setting
			if(init){
				init=false;
				os_set[0]=sst_pos_init_mr(&newer_mr[read_arg1.from], read_arg1.param,
						TARGETREADNUM(read_arg1), 
						version_order_to_version(LSM.last_run_version, des->idx, big_order),
						read_map_done_check, invalid_map_done);
				os_set[1]=sst_pos_init_mr(&older_mr[read_arg2.from], read_arg2.param,
						TARGETREADNUM(read_arg2), 
						version_order_to_version(LSM.last_run_version, des->idx, small_order),
						read_map_done_check, invalid_map_done);
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

			issue_map_read_sst_job(cm, &thread_arg);
			
			if(newer_mr && older_mr){
				border_lba=MIN(newer_mr[read_arg1.to].end_lba, 
					older_mr[read_arg2.to].end_lba);
			}
			else{
				border_lba=(newer_mr?newer_mr[read_arg1.to].end_lba:
						older_mr[read_arg2.to].end_lba);
			}

			if(newer_mr){
				stream_newer_border_lba=newer_mr[read_arg1.to].end_lba;
			}
			
			if(older_mr){
				stream_older_border_lba=older_mr[read_arg2.to].end_lba;
			}

			//sorting
			LSM.monitor.merge_valid_entry_cnt+=stream_sorting(NULL, MERGED_RUN_NUM, os_set, NULL, kpq, 
				last_round_check,
				border_lba,/*limit*/
				target_version, 
				true,
				UINT32_MAX,
				tiering_invalidation_function, false);

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
			LSM.now_compaction_bos=bos;
		}
		if(bis==NULL){
			bis=tiering_new_bis(locked_seg_q, des->idx);
		}

		uint32_t entry_num=issue_read_kv_for_bos_sorted_set(bos, kpq, &border_lba,
				true, idx_set[1], idx_set[0], last_round_check);
	
		uint32_t written_border_lba=issue_write_kv_for_bis(&bis, bos, 
				locked_seg_q, new_run, entry_num, 
				target_version, last_round_check, &gced);
		if(!last_round_check){
			if(written_border_lba==UINT32_MAX){
				//there are no pages to write	
			}
			else{
				border_lba=written_border_lba;
			}
		}
		else{
			border_lba=written_border_lba;
		}

		map_range_postprocessing(new_range_set, border_lba, last_round_check, true, true);
		map_range_postprocessing(old_range_set, border_lba, last_round_check, true, true);

		newer_sst_idx=newer_sst_idx_end+1;
		older_sst_idx=older_sst_idx_end+1;

		if(gced){
			if(newer->update_by_gc){
				newer_sst_idx=run_retrieve_upper_bound_sst_idx(newer, stream_newer_border_lba);
				os_set[0]->gced_out_stream++;
			}

			if(older->update_by_gc){
				older_sst_idx=run_retrieve_upper_bound_sst_idx(older, stream_older_border_lba);
				os_set[1]->gced_out_stream++;
			}
		}
		
		read_arg1_prev=read_arg1;
		read_arg2_prev=read_arg2;
	}

	thpool_wait(cm->issue_worker);

	sst_file *last_file;
	if((last_file=bis_to_sst_file(bis))){
		run_append_sstfile_move_originality(new_run, last_file);
		sst_free(last_file, LSM.pm);
	}

	sst_bis_free(bis);
	sst_bos_free(bos, _cm);

	release_locked_seg_q(locked_seg_q);
	LSM.monitor.merge_total_entry_cnt+=os_set[0]->total_poped_num+
		os_set[1]->total_poped_num;

	sst_pos_free(os_set[0]);
	sst_pos_free(os_set[1]);
	delete kpq;
	free(thread_arg.arg_set);

	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx, des->max_contents_num, des->check_full_by_size);

	run *rptr; uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(ridx!=idx_set[0] && ridx!=idx_set[1]){
			if(rptr->now_sst_num || rptr->sst_num_zero_by_gc){
				level_append_run_copy_move_originality(res, rptr, ridx);
			}
		}
	}

	version_clear_merge_target(LSM.last_run_version, idx_set, des->idx);
	printf("prev - merge :%u run_num:%u new_run sst_size:%u\n", cnt, res->run_num, new_run->now_sst_num);
	if(new_run->now_sst_num){
		level_update_run_at_move_originality(res, target_ridx, new_run, true);
		version_repopulate_merge_target(LSM.last_run_version, idx_set[0], target_ridx, des->idx);
	}
	else{
	//	level_run_reinit(res, target_ridx);
	}
	printf("after - merge :%u run_num:%u new_run sst_size:%u\n", cnt++, res->run_num, new_run->now_sst_num);

	run_free(new_run);

	//version_poped_update(LSM.last_run_version, des->idx, MERGED_RUN_NUM);

	printf("merge %u,%u to %u\n", idx_set[0], idx_set[1], target_ridx);
	delete new_range_set;
	delete old_range_set;

	LSM.now_merging_run[0]=UINT32_MAX;
	LSM.now_merging_run[1]=UINT32_MAX;
	
	lsmtree_after_compaction_processing(&LSM);

	return res;
}

uint32_t update_read_arg_tiering(uint32_t read_done_flag, bool isfirst,sst_pf_out_stream **pos_set, 
		map_range **mr_set, read_issue_arg *read_arg_set, bool invalid_map,
		uint32_t stream_num, level *src, uint32_t version){
	uint32_t remain_num=0;
	for(uint32_t i=0; i<stream_num; i++){
		if(read_done_flag & (1<<i)) continue;
		else remain_num++;
	}

	for(uint32_t i=0 ; i<stream_num; i++){
		if(read_done_flag & (1<<i)) continue;
		if(read_arg_set[i].max_num==0){
			read_arg_set[i].to=0;
			read_arg_set[i].from=read_arg_set[i].to+1;
			read_done_flag|=(1<<i);
			if(!isfirst){
				continue;
			}
		}
		if(!isfirst && read_arg_set[i].to==read_arg_set[i].max_num-1){
			read_arg_set[i].from=read_arg_set[i].to+1;
			read_done_flag|=(1<<i);
			continue;
		}

		if(isfirst){
			uint32_t target_version=src?version_order_to_version(LSM.last_run_version, src->idx, stream_num-1-i): 0;
			//printf("debugging needed target_version: %u\n", target_version);
			if(read_arg_set[i].max_num){
				read_arg_set[i].from=0;
				read_arg_set[i].to=MIN(read_arg_set[i].from+COMPACTION_TAGS/remain_num-1, 
						read_arg_set[i].max_num-1);
			}
			read_map_param_init(&read_arg_set[i], mr_set[i]);
			pos_set[i]=sst_pos_init_mr(&mr_set[i][read_arg_set[i].from], 
					read_arg_set[i].param, TARGETREADNUM(read_arg_set[i]),
					target_version,
					read_map_done_check, invalid_map? invalid_map_done:not_invalid_map_done);
		}
		else{
			read_arg_set[i].from=read_arg_set[i].to+1;
			read_arg_set[i].to=MIN(read_arg_set[i].from+COMPACTION_TAGS/remain_num-1, 
					read_arg_set[i].max_num-1);
			read_map_param_init(&read_arg_set[i], mr_set[i]);
			sst_pos_add_mr(pos_set[i], &mr_set[i][read_arg_set[i].from], 
					read_arg_set[i].param, TARGETREADNUM(read_arg_set[i]));
		}
	}
	return read_done_flag;
}

run* tiering_trivial_move(level *src, uint32_t des_idx, uint32_t max_run_num, 
		uint32_t target_version, bool hot_cold_mode, bool inplace){
	run *src_rptr; uint32_t src_ridx;
	uint32_t max_lba=0, min_lba=UINT32_MAX;
	/*check run sequential*/
	for(uint32_t i=0; i<max_run_num; i++){
		src_ridx=version_order_to_ridx(LSM.last_run_version, src->idx, i);
		src_rptr=LEVEL_RUN_AT_PTR(src, src_ridx);
		if(src_rptr->end_lba > max_lba){
			max_lba=src_rptr->end_lba;
		}
		if(src_rptr->start_lba<min_lba){
			min_lba=src_rptr->start_lba;
		}

		run *comp_rptr; uint32_t comp_idx=src_ridx+1;
		for_each_run_at(src, comp_rptr, comp_idx){
			if((src_rptr->start_lba > comp_rptr->end_lba) ||
				(src_rptr->end_lba<comp_rptr->start_lba)){
				continue;
			}
			else{
				return NULL;
			}
		}
	}

	LSM.monitor.trivial_move_cnt++;
	std::map<uint32_t, run*> temp_run;
	/*filtering page_file*/
	/*run *page_file_run=run_init(src->now_sst_num, 0, UINT32_MAX);*/
	for(uint32_t i=0; i<max_run_num; i++){
		src_ridx=version_order_to_ridx(LSM.last_run_version, src->idx, i);
		src_rptr=LEVEL_RUN_AT_PTR(src, src_ridx);
		uint32_t sidx; sst_file *sptr;
//retry:
		for_each_sst(src_rptr, sptr, sidx){
			if(sptr->type==PAGE_FILE){
				return NULL;
	//			run_append_sstfile_move_originality(page_file_run, sptr);
	//			run_remove_sst_file_at(src_rptr, sidx);
//				goto retry;
			}
		}
		temp_run.insert(std::pair<uint32_t, run*>(src_rptr->start_lba, src_rptr));
	}

	run *res=run_init(src->max_sst_num, UINT32_MAX, 0);
	std::map<uint32_t, run*>::iterator iter;

	//compaction_convert_sst_page_to_block(page_file_run);
	read_helper_param rhp=lsmtree_get_target_rhp(des_idx);
	for(iter=temp_run.begin(); iter!=temp_run.end(); iter++){
		run *rptr=iter->second;
		if(target_version!=UINT32_MAX && src->idx!=UINT32_MAX){
			/*filtering sequential file*/
			run *temp_rptr=run_init(rptr->now_sst_num, UINT32_MAX, 0);
			uint32_t sidx;
			sst_file *temp_sptr;
			uint32_t *moved_sidx=(uint32_t*)calloc(rptr->now_sst_num, sizeof(uint32_t));
			for_each_sst(rptr, temp_sptr, sidx){
				if(temp_sptr->_read_helper && rhp.type==temp_sptr->_read_helper->type){
					uint32_t contents_num=read_helper_get_cnt(temp_sptr->_read_helper);
					float density=(float)(contents_num-1)/(temp_sptr->end_lba-temp_sptr->start_lba);
					if(density >= 0.9){
						version_update_for_trivial_move(LSM.last_run_version,
								temp_sptr->start_lba, temp_sptr->end_lba,
								src->idx, des_idx, target_version);
						moved_sidx[sidx]=UINT32_MAX;
						continue;
					}
				}

				moved_sidx[sidx]=temp_rptr->now_sst_num;
				run_append_sstfile(temp_rptr, temp_sptr);
			}

			if(temp_rptr->now_sst_num){
				compaction_trivial_move(temp_rptr, target_version, src->idx, des_idx, inplace);
				for_each_sst(rptr, temp_sptr, sidx){
					if(moved_sidx[sidx]==UINT32_MAX) continue;
					else{
						temp_sptr->_read_helper=temp_rptr->sst_set[moved_sidx[sidx]]._read_helper;	
					}
				}
			}
			
			run_free(temp_rptr);
			free(moved_sidx);
			
		}
		
		sst_file *sptr; uint32_t sidx;
		for_each_sst(rptr, sptr, sidx){
			if(des_idx==1 && sptr->type==PAGE_FILE){
				printf("break?\n");
			}
			run_append_sstfile_move_originality(res, sptr);
		}
	}


	return res;
}

static inline uint32_t get_border_from_read_arg_set(read_issue_arg *arg_set, map_range **mr_set, 
		uint32_t stream_num, uint32_t read_done){
	uint32_t res=UINT32_MAX;
	if(read_done==(1<<stream_num)-1){
		return res;
	}
	for(int i=0; i<stream_num; i++){
		if(read_done & 1<<i) continue;
		uint32_t target=(arg_set[i].to==arg_set[i].max_num-1) ? UINT32_MAX:mr_set[i][arg_set[i].to].end_lba;
		if(res>target){
			res=target;
		}
	}
	return res;
}

run **compaction_TI2RUN(compaction_master *cm, level *src, level *des, uint32_t merging_num, 
		uint32_t target_demote_version, uint32_t target_keep_version, bool *issequential, bool inplace, bool hot_cold_mode){
	_cm=cm;
	run **new_run;
	if(hot_cold_mode){
		new_run=(run**)calloc(2, sizeof(run*));
		new_run[DEMOTE_RUN]=run_init(src->max_sst_num, UINT32_MAX, 0);
		new_run[KEEP_RUN]=run_init(src->max_sst_num, UINT32_MAX, 0);
	}else{
		new_run=(run**)calloc(1, sizeof(run*));
		new_run[DEMOTE_RUN]=run_init(src->max_sst_num, UINT32_MAX, 0);
	}
	uint32_t stream_num=merging_num;
	run *temp_new_run;
	if((temp_new_run=tiering_trivial_move(src, des->idx, merging_num, target_demote_version, 
					hot_cold_mode, inplace))){
		run_free(new_run[DEMOTE_RUN]);
		new_run[DEMOTE_RUN]=temp_new_run;
		*issequential=true;
		return new_run;
	}
	*issequential=false;
	
	read_issue_arg *read_arg_set=(read_issue_arg*)calloc(stream_num, sizeof(read_issue_arg));
	read_arg_container thread_arg;
	thread_arg.end_req=merge_end_req;
	thread_arg.arg_set=(read_issue_arg**)calloc(stream_num, sizeof(read_issue_arg*));
	for(int32_t i=stream_num-1; i>=0; i--){
		thread_arg.arg_set[i]=&read_arg_set[i];
	}
	thread_arg.set_num=stream_num;

	LSM.read_arg_set=thread_arg.arg_set;
	LSM.now_compaction_stream_num=stream_num;

#ifdef LSM_DEBUG
	if(version_to_belong_level(LSM.last_run_version, target_demote_version)!=des->idx){
		EPRINT("demote version should be belong to des", true);
	}

	if(version_to_belong_level(LSM.last_run_version, target_keep_version)!=src->idx){
		EPRINT("keep version should be belong to src", true);
	}
#endif

#ifdef LSM_DEBUG
	//version_print_order(LSM.last_run_version, src->idx);
#endif

	static uint32_t cnt=0;
	printf("TI2RUN %u\n", cnt++);
	if(cnt==14){
	}
	map_range **mr_set=(map_range **)calloc(stream_num, sizeof(map_range*));
	/*make it reverse order for stream sorting*/
	//printf("target ridx print\n"); 
	for(int32_t i=stream_num-1, j=0; i>=0; i--, j++){
		uint32_t ridx=version_order_to_ridx(LSM.last_run_version, src->idx, i);
		uint32_t sst_file_num=src->array[ridx].now_sst_num;
		uint32_t map_num=0;
		//printf("\tridx:%u\n", ridx);
		for(uint32_t j=0; j<sst_file_num; j++){
			if(src->array[ridx].sst_set[j].type==PAGE_FILE){
				map_num++;
				if(src->array[ridx].sst_set[j].map_num){
					EPRINT("why??", true);
				}
			}
			else{
				map_num+=src->array[ridx].sst_set[j].map_num;
			}
		}
		/*
		if(!map_num){
			abort();
		}*/
		read_arg_set[j].max_num=map_num;
		mr_set[j]=make_mr_set(src->array[ridx].sst_set, 0, src->array[ridx].now_sst_num-1, map_num);
		read_arg_set[j].version_for_gc=version_order_to_version(LSM.last_run_version,  src->idx, i);
		read_arg_set[j].page_file=false;
	}

	std::queue<key_ptr_pair> *kpq=new std::queue<key_ptr_pair>();
	
	std::queue<uint32_t> *locked_seg_q=new std::queue<uint32_t>();

	std::list<mr_free_set> **MFS_set_ptr=new std::list<mr_free_set> *[stream_num]();
	for(uint32_t i=0; i<stream_num; i++){
		MFS_set_ptr[i]=new std::list<mr_free_set>();
	}

	uint32_t sorting_done=0;
	uint32_t read_done=0;
	sst_pf_out_stream **pos_set=(sst_pf_out_stream **)calloc(stream_num, sizeof(sst_pf_out_stream*));
	LSM.compactioning_pos_set=pos_set;

	sst_bf_out_stream *bos=NULL;
	sst_bf_in_stream **bis=NULL;
	if(hot_cold_mode){
		bis=(sst_bf_in_stream**)calloc(2, sizeof(sst_bf_in_stream*));
	}
	else{
		bis=(sst_bf_in_stream**)calloc(1, sizeof(sst_bf_in_stream*));
	}
	bool isfirst=true;

	uint32_t border_lba=UINT32_MAX;
	uint32_t *stream_border_lba_set=(uint32_t*)malloc(sizeof(uint32_t)*stream_num);
	memset(stream_border_lba_set, UINT8_MAX, sizeof(uint32_t) * stream_num);
	bool bis_populate=false;
//	uint32_t round=0;
	uint32_t skip_target_version=UINT32_MAX;
	if(hot_cold_mode && merging_num!=src->run_num ){
		skip_target_version=version_order_to_version(LSM.last_run_version, src->idx, merging_num);
	}

	bool gced=false;
	while(!(sorting_done==((1<<stream_num)-1) && read_done==((1<<stream_num)-1))){

		if(gced){
			for(int32_t i=stream_num-1, j=0; i>=0; i--, j++){
				if(read_done & (1<<j)) continue;
				if(LSM.global_debug_flag && j==12){
					printf("break!\n");
				}
				uint32_t ridx=version_order_to_ridx(LSM.last_run_version, src->idx, i);
				compaction_adjust_by_gc(&read_arg_set[j], pos_set[j], stream_border_lba_set[j], 
						&src->array[ridx], &mr_set[j], BLOCK_FILE, false);
				src->array[ridx].update_by_gc=false;
			}
			/*check finish*/
		}
		gced=false;

		read_done=update_read_arg_tiering(read_done, isfirst, pos_set, mr_set,
				read_arg_set, true, stream_num, src, UINT32_MAX);

//	printf("round:%u\n", round++);

		for(uint32_t k=0; k<stream_num; k++){
			if(read_done & (1<<k)) continue;
			mr_free_set temp_MFS=making_MFS(&read_arg_set[k], mr_set[k], LEVEL_RUN_AT_PTR(src, stream_num-1-k));
			map_range_preprocessing(temp_MFS, MFS_set_ptr[k], false);
			stream_border_lba_set[k]=mr_set[k][read_arg_set[k].to].end_lba;
		}

		bool last_round=(read_done==(1<<stream_num)-1);
		if(!last_round){
			issue_map_read_sst_job(cm, &thread_arg);
		}

		border_lba=get_border_from_read_arg_set(read_arg_set, mr_set, stream_num, read_done);
		if(border_lba==UINT32_MAX){
			last_round=true;
		}

		uint32_t sorted_entry_num=stream_sorting(des, stream_num, pos_set, NULL, kpq, 
				last_round,
				border_lba,/*limit*/
				target_demote_version, 
				true,
				hot_cold_mode? skip_target_version:UINT32_MAX,
				tiering_invalidation_function,
				inplace);


		if(bos==NULL){
			bos=sst_bos_init(read_map_done_check, true);
			LSM.now_compaction_bos=bos;
		}

		if(!bis_populate){
			if(hot_cold_mode){
				bis[0]=tiering_new_bis(locked_seg_q, inplace?src->idx: des->idx);	
				bis[1]=tiering_new_bis(locked_seg_q, inplace?src->idx: des->idx);			
			}
			else{
				bis[0]=tiering_new_bis(locked_seg_q, inplace?src->idx: des->idx);	
			}
			bis_populate=true;
		}
		
		if(last_round && sorted_entry_num==0){
			uint32_t read_num=issue_read_kv_for_bos_sorted_set(bos, kpq, &border_lba,
					false, UINT32_MAX, UINT32_MAX, last_round);

			thpool_wait(cm->issue_worker);
			for(uint32_t k=0; k<stream_num; k++){
				map_range_postprocessing(MFS_set_ptr[k], border_lba, last_round, false, false);
			}

			if(hot_cold_mode){
				border_lba=issue_write_kv_for_bis_hot_cold(&bis, bos, locked_seg_q, new_run, 
						read_num, target_demote_version, target_keep_version, src->idx, last_round, &gced);			
			}else{
				border_lba=issue_write_kv_for_bis(&bis[DEMOTE_RUN], bos, locked_seg_q, new_run[DEMOTE_RUN], 
						read_num, target_demote_version, last_round, &gced);
			}
		}
		else{
			for(uint32_t moved_num=0; moved_num<sorted_entry_num; ){
				uint32_t read_num=issue_read_kv_for_bos_sorted_set(bos, kpq, &border_lba,
						false, UINT32_MAX, UINT32_MAX, last_round);

				for(uint32_t k=0; k<stream_num; k++){
					map_range_postprocessing(MFS_set_ptr[k], border_lba, last_round, false, false);
				}

				thpool_wait(cm->issue_worker);
				if(hot_cold_mode){
					border_lba=issue_write_kv_for_bis_hot_cold(&bis, bos, locked_seg_q, new_run, 
							read_num, target_demote_version, target_keep_version, src->idx, last_round, &gced);			
				}else{
					border_lba=issue_write_kv_for_bis(&bis[DEMOTE_RUN], bos, locked_seg_q, new_run[DEMOTE_RUN], 
							read_num, target_demote_version, last_round, &gced);
				}
				moved_num+=read_num;
			}
		}

		for(uint32_t k=0; k<stream_num; k++){
			map_range_postprocessing(MFS_set_ptr[k], border_lba, last_round, false, false);
		}

		for(uint32_t i=0; i<stream_num; i++){
			if(!(read_done & (1<<i))) continue;
			if((sorting_done & (1<<i))) continue;
			if(sst_pos_is_empty(pos_set[i])){
				sorting_done |=(1<<i);
			}
		}
		isfirst=false;
	}

	sst_file *last_file;
	if(hot_cold_mode){
		if((last_file=bis_to_sst_file(bis[DEMOTE_RUN]))){
			run_append_sstfile_move_originality(new_run[DEMOTE_RUN], last_file);
			sst_free(last_file, LSM.pm);
		}

		if((last_file=bis_to_sst_file(bis[KEEP_RUN]))){
			run_append_sstfile_move_originality(new_run[KEEP_RUN], last_file);
			sst_free(last_file, LSM.pm);
		}
	}
	else{
		if((last_file=bis_to_sst_file(bis[DEMOTE_RUN]))){
			run_append_sstfile_move_originality(new_run[DEMOTE_RUN], last_file);
			sst_free(last_file, LSM.pm);
		}
	}

	for(uint32_t i=0; i<stream_num; i++){
		LSM.monitor.tiering_total_entry_cnt[des->idx]+=pos_set[i]->total_poped_num;
	}
	LSM.monitor.tiering_valid_entry_cnt[des->idx]+=new_run[DEMOTE_RUN]->now_contents_num;

	for(uint32_t i=0; i<stream_num; i++){
		sst_pos_free(pos_set[i]);
	}

	release_locked_seg_q(locked_seg_q);
	for(uint32_t i=0; i<stream_num; i++){
		free(mr_set[i]);
		delete MFS_set_ptr[i];
	}
	delete[] MFS_set_ptr;

	if(hot_cold_mode){
		sst_bis_free(bis[DEMOTE_RUN]);
		if(new_run[KEEP_RUN]->now_sst_num==0){
			read_helper_free(bis[KEEP_RUN]->rh);
		}
		sst_bis_free(bis[KEEP_RUN]);
	}else{
		sst_bis_free(bis[DEMOTE_RUN]);
	}

	lsmtree_after_compaction_processing(&LSM);
	
	sst_bos_free(bos, _cm);
	delete kpq;
	free(stream_border_lba_set);
	free(bis);
	free(pos_set);
	free(mr_set);
	free(thread_arg.arg_set);
	free(read_arg_set);
	return new_run;
}

level* compaction_TI2TI(compaction_master *cm, level *src, level *des, uint32_t target_version, bool *populated){
	_cm=cm;
	bool issequential;
	run **new_run=compaction_TI2RUN(cm, src, des, src->run_num, target_version, UINT32_MAX, &issequential, false, false);
	uint32_t target_run_idx=version_to_ridx(LSM.last_run_version, des->idx, target_version);
	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx, 
			des->max_contents_num, des->check_full_by_size);

	run *rptr;
	uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(rptr->now_sst_num || rptr->sst_num_zero_by_gc){
			level_append_run_copy_move_originality(res, rptr, ridx);
		}
	}

	if(new_run[0]->now_sst_num){
		level_update_run_at_move_originality(res, target_run_idx, new_run[0], true);
		*populated=true;
	}
	else{
		*populated=false;
	}

	run_free(new_run[DEMOTE_RUN]);
	free(new_run);

	level_print(res, false);
	return res;
}

level *compaction_TI2TI_separation(compaction_master *cm, level *src, level *des,
		uint32_t target_version, bool *hot_cold_separation, bool *populated){
	bool issequential;
	uint32_t target_run_num=src->run_num;
	uint32_t target_keep_version=version_pick_oldest_version(LSM.last_run_version, src->idx);
	run **new_run=compaction_TI2RUN(cm, src, des, target_run_num,  
			target_version, target_keep_version, &issequential, false, true);

	uint32_t target_run_idx=version_to_ridx(LSM.last_run_version, des->idx, target_version);
	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx, 
			des->max_contents_num, des->check_full_by_size);

	uint32_t ridx;
	run * rptr;
	for_each_run_max(des, rptr, ridx){
		if(rptr->now_sst_num || rptr->sst_num_zero_by_gc){
			level_append_run_copy_move_originality(res, rptr, ridx);
		}
	}


	if(new_run[DEMOTE_RUN]->now_sst_num){
		level_update_run_at_move_originality(res, target_run_idx, new_run[DEMOTE_RUN], true);
		*populated=true;
	}
	else{
		*populated=false;
	}
	/*
	if(issequential) {
		*hot_cold_separation=false;
		return res;
	}
	 */


//	run_content_print(new_run[0], true);
	*hot_cold_separation=true;

//	level_content_print(src, true);

	if(target_run_num==src->run_num){
		for(uint32_t i=0; i<target_run_num; i++){
			level_run_reinit(src, i);
		}
		version_clear_level(LSM.last_run_version, src->idx);
	}
	else{
		EPRINT("not allow", true);
	}

	version_poped_update(LSM.last_run_version, src->idx, target_run_num);
#ifdef LSM_DEBUG
	//version_print_order(LSM.last_run_version, src->idx);
#endif

	if(new_run[KEEP_RUN]->now_sst_num){
		uint32_t target_real_keep_version=version_get_empty_version(LSM.last_run_version, src->idx);
		if(target_real_keep_version!=target_keep_version){
			EPRINT("version is differ from target_keep_version", true);
		}
		uint32_t src_ridx=version_to_ridx(LSM.last_run_version, src->idx, target_real_keep_version);
		level_update_run_at_move_originality(src, src_ridx, new_run[KEEP_RUN], true);
		version_populate(LSM.last_run_version, target_real_keep_version, src->idx);
	}

	run_free(new_run[KEEP_RUN]);
	run_free(new_run[DEMOTE_RUN]);
	free(new_run);
	level_print(src,false);
//	level_print(res);
	return res;
}

level *compaction_TW_convert_LW(compaction_master *cm, level *src){
	_cm=cm;
	run *new_run=NULL;
	uint32_t stream_num=src->run_num;
	level *res=level_init(src->now_sst_num, 1, LEVELING_WISCKEY, src->idx, src->max_contents_num, src->check_full_by_size);
	if((new_run=tiering_trivial_move(src, UINT32_MAX, src->run_num, UINT32_MAX, false, false))){
		sst_file *sptr; uint32_t sidx;
		for_each_sst(new_run, sptr, sidx){
			level_append_sstfile(res, sptr, true);
		}
		return res;
	}

	read_issue_arg *read_arg_set=(read_issue_arg*)calloc(stream_num, sizeof(read_issue_arg));
	read_arg_container thread_arg;
	thread_arg.end_req=merge_end_req;
	thread_arg.arg_set=(read_issue_arg**)calloc(stream_num, sizeof(read_issue_arg*));
	for(int32_t i=stream_num-1; i>=0; i--){
		thread_arg.arg_set[i]=&read_arg_set[i];
	}
	thread_arg.set_num=stream_num;

	map_range **mr_set=(map_range **)calloc(stream_num, sizeof(map_range*));
	run *rptr; uint32_t ridx; uint32_t set_idx=0, map_num;
	/*make it reverse order for stream sorting*/
	for_each_run_reverse(src, rptr, ridx){
		mr_set[set_idx]=run_to_MR(rptr, &map_num);
		read_arg_set[set_idx].max_num=map_num;
		set_idx++;
	}

	sst_pf_out_stream **pos_set=(sst_pf_out_stream **)calloc(stream_num, sizeof(sst_pf_out_stream*));
	uint32_t read_done=0;
	uint32_t sorting_done=0;
	bool isfirst=true;
	sst_pf_in_stream *pis=sst_pis_init(true, lsmtree_get_target_rhp(src->idx));
	uint32_t target_version=version_order_to_version(LSM.last_run_version, src->idx, src->run_num-1);

	uint32_t border_lba=UINT32_MAX;
	while(!(sorting_done==((1<<stream_num)-1) && read_done==((1<<stream_num)-1))){
		read_done=update_read_arg_tiering(read_done, isfirst, pos_set, mr_set,
				read_arg_set, true, stream_num, src, UINT32_MAX);
		bool last_round=(read_done==(1<<stream_num)-1);

		if(!last_round){
			issue_map_read_sst_job(cm, &thread_arg);
		}
	
		border_lba=get_border_from_read_arg_set(read_arg_set, mr_set, stream_num, read_done);

		stream_sorting(res, stream_num, pos_set, pis, NULL,
				last_round,
				border_lba,/*limit*/
				target_version, 
				true,
				UINT32_MAX,
				tiering_invalidation_function, false);

		for(uint32_t i=0; i<stream_num; i++){
			if(!(read_done & (1<<i))) continue;
			if((sorting_done & (1<<i))) continue;
			if(sst_pos_is_empty(pos_set[i])){
				sorting_done |=(1<<i);
			}
		}
		isfirst=false;
	}

	thpool_wait(cm->issue_worker);

	for(uint32_t i=0; i<stream_num; i++){
		free(mr_set[i]);
		sst_pos_free(pos_set[i]);
	}

	sst_pis_free(pis);
	free(pos_set);

	free(mr_set);
	free(thread_arg.arg_set);
	free(read_arg_set);
	return res;
}

static uint32_t filter_invalidation(sst_pf_out_stream *pos, std::queue<key_ptr_pair> *kpq, 
		uint32_t now_version){
	uint32_t valid_num=0;
	while(!sst_pos_is_empty(pos)){
		key_ptr_pair target_pair=sst_pos_pick(pos);
		if(target_pair.lba==UINT32_MAX){
			continue;
		}
		uint32_t recent_version=version_map_lba(LSM.last_run_version, target_pair.lba);
		if(now_version==recent_version || 
				version_compare(LSM.last_run_version, now_version, recent_version)>0){
			kpq->push(target_pair);
			valid_num++;
		}
		else{
			invalidate_kp_entry(target_pair.lba, target_pair.piece_ppa, now_version, true);
		}
		sst_pos_pop(pos);
	}
	return valid_num;
}

static inline void gc_lock_run(run *r){
#ifdef DEMAND_SEG_LOCK
#else
	sst_file *sptr;
	uint32_t idx;
	for_each_sst(r, sptr, idx){
	//	printf("lock lba:%u ->sidx:%u segidx:%u\n", sptr->end_lba, idx, sptr->end_ppa/_PPS);
		lsmtree_gc_unavailable_set(&LSM, sptr, UINT32_MAX);
	}
#endif
}

static inline void gc_unlock_run(run *r, uint32_t *sst_file_num, uint32_t border_lba){
	if(r->now_sst_num <= *sst_file_num) return;
#ifdef DEMAND_SEG_LOCK
#else
	sst_file *sptr;
	uint32_t idx=*sst_file_num;
	for_each_sst_at(r, sptr, idx){
		if(sptr->end_lba<=border_lba){
	//		printf("unlock lba:%u ->sidx:%u border:%u\n", sptr->end_lba, idx, border_lba);
			lsmtree_gc_unavailable_unset(&LSM, sptr, UINT32_MAX);
		}
		else{
			break;
		}
	}
	*sst_file_num=idx;
#endif
}

run *compaction_reclaim_run(compaction_master *cm, run *target_rptr, uint32_t version){
	_cm=cm;
	read_issue_arg read_arg_set;
	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg));
	thread_arg.arg_set[0]=&read_arg_set;
	thread_arg.set_num=1;

	sst_pf_out_stream *pos=NULL;
	map_range *mr_set;
	uint32_t map_num;
	mr_set=run_to_MR(target_rptr, &map_num);
	uint32_t stream_num=1;
	uint32_t read_done=0;
	bool isfirst=true;
	bool last_round=false;
	std::queue<key_ptr_pair> *kpq=new std::queue<key_ptr_pair>();
	std::queue<uint32_t> *locked_seg_q=new std::queue<uint32_t>();
	sst_bf_out_stream *bos=NULL;
	sst_bf_in_stream *bis=NULL;
	uint32_t border_lba=0;

	LSM.read_arg_set=thread_arg.arg_set;
	LSM.now_compaction_stream_num=1;
	LSM.compactioning_pos_set=&pos;

	read_arg_set.max_num=map_num;
	read_arg_set.version_for_gc=version;
	read_arg_set.page_file=false;


	run *new_run=run_init(target_rptr->now_sst_num, UINT32_MAX, 0);

	uint32_t unlocked_sst_idx=0;
	bool gced=false;

	if(LSM.global_debug_flag){
		printf("break!\n");
	}

	uint32_t stream_border_lba=0;
	while(read_done!=(1<<stream_num)-1){
		if(gced){
			compaction_adjust_by_gc(&read_arg_set, pos, stream_border_lba,
					target_rptr, &mr_set, BLOCK_FILE, false);
		}
		gced=false;
		read_done=update_read_arg_tiering(read_done, isfirst, &pos, &mr_set, 
				&read_arg_set, true, stream_num, NULL, version);

		last_round=(read_done==(1<<stream_num)-1);

		if(!last_round){
			stream_border_lba=mr_set[read_arg_set.to].end_lba;
		}
		else{
			stream_border_lba=UINT32_MAX;
		}

		if(!last_round){
			issue_map_read_sst_job(cm, &thread_arg);
		}

		filter_invalidation(pos, kpq, version);

		if(bos==NULL){
			bos=sst_bos_init(read_map_done_check, true);
			LSM.now_compaction_bos=bos;
		}
		if(bis==NULL){
			bis=tiering_new_bis(locked_seg_q, LSM.param.LEVELN-1);	
		}

		uint32_t read_num=issue_read_kv_for_bos_sorted_set(bos, kpq, &border_lba,
				false, UINT32_MAX, UINT32_MAX, last_round);
		border_lba=issue_write_kv_for_bis(&bis, bos, locked_seg_q, new_run, 
				read_num, version, last_round, &gced);
		
		isfirst=false;
	}
	thpool_wait(cm->issue_worker);
	/*finishing*/

	sst_file *last_file;
	if((last_file=bis_to_sst_file(bis))){
		run_append_sstfile_move_originality(new_run, last_file);
		sst_free(last_file, LSM.pm);
	}

	sst_bis_free(bis);
	sst_bos_free(bos, _cm);

	release_locked_seg_q(locked_seg_q);
	delete kpq;
	sst_pos_free(pos);
	free(mr_set);
	free(thread_arg.arg_set);
	lsmtree_gc_unavailable_sanity_check(&LSM);
	lsmtree_after_compaction_processing(&LSM);
	return new_run;
}

static inline void make_rh_for_trivial_move(sst_file *sptr, 
		std::queue<key_ptr_pair>*kp_q, uint32_t to_lev_idx){
	read_helper_param rhp=lsmtree_get_target_rhp(to_lev_idx);
	rhp.member_num=kp_q->size();
	read_helper *rh=read_helper_init(rhp);

	while(!kp_q->empty()){
		key_ptr_pair target_pair=kp_q->front();
		read_helper_stream_insert(rh, target_pair.lba, target_pair.piece_ppa);
		kp_q->pop();
	}

	read_helper_insert_done(rh);
	sptr->_read_helper=rh;
}

sst_file *trivial_move_processing(run *rptr, sst_pf_out_stream *pos, 
		read_issue_arg *read_arg, 
		uint32_t target_version, uint32_t from_lev_idx, 
		uint32_t to_lev_idx, std::queue<key_ptr_pair> *kp_q, bool make_rh, bool inplace){
	version *v=LSM.last_run_version;
	sst_file *sptr=NULL;
	uint32_t min_lba, max_lba;

	while(!sst_pos_is_empty(pos)){
		key_ptr_pair target_pair=sst_pos_pick(pos);
		if(target_pair.lba==UINT32_MAX){
			continue;
		}

		if(sptr==NULL || !(target_pair.lba >=min_lba && target_pair.lba<=max_lba)){
			if(sptr && make_rh){
				make_rh_for_trivial_move(sptr, kp_q, inplace?from_lev_idx:to_lev_idx);
			}
			sptr=run_retrieve_sst(rptr, target_pair.lba);

			min_lba=sptr->start_lba;
			max_lba=sptr->end_lba;
		}

		uint32_t recent_version=version_map_lba(v, target_pair.lba);
		if(from_lev_idx==UINT32_MAX || version_belong_level(v, recent_version, from_lev_idx)){
			version_coupling_lba_version(v, target_pair.lba, target_version);
		}

		if(make_rh){
			kp_q->push(target_pair);
		}
		sst_pos_pop(pos);
	}
	return sptr;
}

void compaction_trivial_move(run *rptr, uint32_t target_version, uint32_t from_lev_idx, 
		uint32_t to_lev_idx, bool inplace){
	_cm=LSM.cm;
	read_issue_arg read_arg_set;
	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg));
	thread_arg.arg_set[0]=&read_arg_set;
	thread_arg.set_num=1;

	map_range *mr_set;
	uint32_t map_num=0;
	mr_set=run_to_MR(rptr, &map_num);
	uint32_t stream_num=1;
	uint32_t read_done=0;
	bool isfirst=true;
	bool last_round=false;

	read_helper_param rhp1=lsmtree_get_target_rhp(from_lev_idx);
	read_helper_param rhp2=lsmtree_get_target_rhp(to_lev_idx);
	bool make_rh=rhp1.type!=rhp2.type;

	if(make_rh){ //free rh
		uint32_t sidx;
		sst_file *sptr;
		for_each_sst(rptr, sptr, sidx){
			read_helper_free(sptr->_read_helper);
			sptr->_read_helper=NULL;
		}
	}

	read_arg_set.max_num=map_num;
	sst_pf_out_stream *pos=NULL;

	std::queue<key_ptr_pair> *kp_q=NULL;
	if(make_rh){
		kp_q=new std::queue<key_ptr_pair>();
	}

	sst_file *sptr;
	while(read_done!=(1<<stream_num)-1){
		read_done=update_read_arg_tiering(read_done, isfirst, &pos, &mr_set, &read_arg_set, 
				false, stream_num, NULL, UINT32_MAX);
		last_round=(read_done==(1<<stream_num)-1);
		if(last_round){
			if(sptr && make_rh){
				make_rh_for_trivial_move(sptr, kp_q, inplace?from_lev_idx:to_lev_idx);
			}
			break;
		}

		issue_map_read_sst_job(_cm, &thread_arg);
		sptr=trivial_move_processing(rptr, pos, &read_arg_set, target_version, 
				from_lev_idx, to_lev_idx, kp_q, make_rh, inplace);
		isfirst=false;
	}

	thpool_wait(_cm->issue_worker);

	if(make_rh){ //free rh
		uint32_t sidx;
		sst_file *sptr;
		for_each_sst(rptr, sptr, sidx){
			read_helper_insert_done(sptr->_read_helper);
			rptr->now_contents_num+=read_helper_get_cnt(sptr->_read_helper);
		}
	}
	
	if(make_rh){
		delete kp_q;
	}
	sst_pos_free(pos);
	free(thread_arg.arg_set);
	free(mr_set);
}

void *merge_end_req(algo_req *req){
	inter_read_alreq_param *r_param;
	key_value_wrapper *kv_wrapper;
	switch(req->type){
		case MAPPINGR:
			r_param=(inter_read_alreq_param*)req->param;
			r_param->map_target->data=r_param->data->value;
			r_param->map_target->read_done=true;
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

/*
static inline uint32_t TI2_RUN_data_move(sst_bf_out_stream *bos, 
		sst_bf_in_stream **bis, 
		std::queue<key_ptr_pair> *kpq,
		std::list<mr_free_set> ** MFS_set_ptr,
		run **new_run;
		std::<uint32_t> *locked_seg_q,
		uint32_t *org_border_lba,
		uint32_t stream_num,
		uint32_t target_version, 
		bool last_round,
		bool hot_cold_mode){

	uint32_t read_num;
	uint32_t border_lba;
	if(hot_cold_mode){		
		read_num=issue_read_kv_for_bos_sorted_set(bos, kpq, org_border_lba,
				false, UINT32_MAX, UINT32_MAX, last_round);

		for(uint32_t k=0; k<stream_num; k++){
			map_range_postprocessing(MFS_set_ptr[k], MAX(border_lba1, border_lba2), last_round, false);
		}
		
		border_lba=issue_write_kv_for_bis_hot_cold(&bis, bos, locked_seg_q, new_run, 
					read_num1, target_version, last_round);
	}
	else{
		read_num=issue_read_kv_for_bos_sorted_set(bos, kpq, org_border_lba,
				false, UINT32_MAX, UINT32_MAX, last_round);

		for(uint32_t k=0; k<stream_num; k++){
			map_range_postprocessing(MFS_set_ptr[k], border_lba, last_round, false);
		}

		border_lba=issue_write_kv_for_bis(&bis[0], bos, locked_seg_q, new_run[0], 
					read_num1, target_version, last_round);
	}
	*org_border_lba=border_lba;
	return read_num;
}*/
