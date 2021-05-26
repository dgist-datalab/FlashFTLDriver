#include "compaction.h"
#include "run.h"
#include "../../include/sem_lock.h"
#include "../../interface/interface.h"
#include "sst_page_file_stream.h"
#include "io.h"
#include "function_test.h"
#include <algorithm>
extern lsmtree LSM;

compaction_master *_cm;
extern uint32_t test_key;
//uint32_t debug_lba=1549288;
uint32_t debug_lba=UINT32_MAX;

extern uint32_t debug_piece_ppa;

static inline void compaction_debug_func(uint32_t lba, uint32_t piece_ppa, uint32_t target_ridx, level *des){
	static int cnt=0;
	if(lba==debug_lba){
		if(piece_ppa==debug_piece_ppa){
			printf("[GOLDEN-same_pice_ppa]");
		}
		if(des){
			if(des->idx==LSM.param.LEVELN-1){
				printf("[%d]%u,%u (l,p) -> %u run-number:%u\n",++cnt, lba,piece_ppa, des->idx, target_ridx);
			}
			else{
				printf("[%d]%u,%u (l,p) -> %u\n",++cnt, lba,piece_ppa, des->idx);
			}
		}
		else{
			printf("[%d]%u,%u (l,p) -> merging to %u\n",++cnt, lba,piece_ppa, target_ridx);
		}
	}
}

static inline void compaction_error_check
(key_ptr_pair *kp_set, level *src, level *des, level *res, uint32_t compaction_num){
	uint32_t min_lba=kp_set?
		MIN(kp_set[0].lba, GET_LEV_START_LBA(des)):
		MIN(GET_LEV_START_LBA(des), GET_LEV_START_LBA(src));

	uint32_t max_lba=kp_set?
		MAX(kp_get_end_lba((char*)kp_set), GET_LEV_END_LBA(des)):
		MAX(GET_LEV_END_LBA(des), GET_LEV_END_LBA(src));

	if(max_lba==UINT32_MAX){
		kp_get_end_lba((char*)kp_set);
	}

	uint32_t target_version=version_level_to_start_version(LSM.last_run_version, des->idx);
	if(!(GET_LEV_START_LBA(res) <= min_lba)){
		if(target_version==version_map_lba(LSM.last_run_version, min_lba)){
			if(src){
				printf("src\n");
				level_print(src);
			}
			printf("des\n");
			level_print(des);
			printf("res\n");
			level_print(res);
			if(kp_set){
				printf("first_compaction_cnt:%u\n", compaction_num);
			}
			else{
				printf("leveling_compaction_cnt:%u\n", compaction_num);
			}
			EPRINT("range error", true);
		}
	}
	if(!(GET_LEV_END_LBA(res)>=max_lba)){
		if(target_version==version_map_lba(LSM.last_run_version,max_lba)){
			printf("src\n");
			level_print(src);
			printf("des\n");
			level_print(des);
			printf("res\n");
			level_print(res);
			if(kp_set){
				printf("first_compaction_cnt:%u\n", compaction_num);
			}
			else{
				printf("leveling_compaction_cnt:%u\n", compaction_num);
			}
			EPRINT("range error", true);
		}
	}

}

static inline void tiering_compaction_error_check(level *src, run *r1, run *r2, run *res,
		uint32_t compaction_num){
	uint32_t min_lba, max_lba;
	if(src){
		min_lba=GET_LEV_START_LBA(src);
		max_lba=GET_LEV_END_LBA(src);
	}
	else{
		min_lba=MIN(r1->start_lba, r2->start_lba);	
		max_lba=MAX(r1->start_lba, r2->start_lba);	
	}	

	if(!(res->start_lba==min_lba && res->end_lba==max_lba)){
		if(src){
			printf("tiering_compaction_cnt:%u\n",compaction_num);
		}
		else{
			printf("merge_compaction_cnt:%u\n",compaction_num);	
		}

		if(res->start_lba!=min_lba){
			if(version_map_lba(LSM.last_run_version,res->start_lba)!=
				version_map_lba(LSM.last_run_version,min_lba)){
				goto out;
			}
		}

		if(res->end_lba!=max_lba){
			if(version_map_lba(LSM.last_run_version,res->end_lba)!=
				version_map_lba(LSM.last_run_version, max_lba)){
				goto out;
			}
		}
		EPRINT("range error", true);
	}
out:
	version_sanity_checker(LSM.last_run_version);
}

void read_sst_job(void *arg, int th_num){
	read_arg_container *thread_arg=(read_arg_container*)arg;
	read_issue_arg **arg_set=thread_arg->arg_set;
	uint32_t *idx_set=(uint32_t*)calloc(thread_arg->set_num, sizeof(uint32_t));
	inter_read_alreq_param ***param_set=
		(inter_read_alreq_param ***)calloc(thread_arg->set_num, sizeof(inter_read_alreq_param**));

	for(uint32_t i=0; i<thread_arg->set_num; i++){
		param_set[i]=arg_set[i]->param;
		idx_set[i]=arg_set[i]->from;
	}

	uint32_t remain_checker=0, target=(1<<(thread_arg->set_num))-1;
	while(!(remain_checker==target)){
		for(uint32_t i=0; i<thread_arg->set_num; i++){

			if(idx_set[i]>(arg_set[i]->to)){
				remain_checker|=1<<i;
			}

			if(remain_checker&1<<i) continue;
			inter_read_alreq_param *now=param_set[i][idx_set[i]-arg_set[i]->from];
			algo_req *read_req=(algo_req*)malloc(sizeof(algo_req));
			//read_req->ppa=now->target->piece_ppa;
			read_req->type=MAPPINGR;
			read_req->param=(void*)now;
			read_req->end_req=thread_arg->end_req;
			if(now->target){
				io_manager_issue_read(now->target->file_addr.map_ppa, 
					now->data, read_req, false);
			}
			else if(now->map_target){
				io_manager_issue_read(now->map_target->ppa, 
					now->data, read_req, false);
			}
			else{
				EPRINT("should_check target!", true);
			}	

			idx_set[i]++;
		}
	}
	free(idx_set);
	free(param_set);
}

static void read_param_init(read_issue_arg *read_arg){
	inter_read_alreq_param *param;
	for(int i=0; i<read_arg->to-read_arg->from+1; i++){
		param=compaction_get_read_param(_cm);
		param->target=LEVELING_SST_AT_PTR(read_arg->des, read_arg->from+i);
		param->data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
		fdriver_lock_init(&param->done_lock, 0);
		read_arg->param[i]=param;
	}
}

bool read_done_check(inter_read_alreq_param *param, bool check_page_sst){
	if(check_page_sst){
		param->target->data=param->data->value;
	}
	fdriver_lock(&param->done_lock);
	return true;
}

bool file_done(inter_read_alreq_param *param){
	param->target->data=NULL;
	inf_free_valueset(param->data, FS_MALLOC_R);
	fdriver_destroy(&param->done_lock);
	invalidate_map_ppa(LSM.pm->bm, param->target->file_addr.map_ppa, true);
	compaction_free_read_param(_cm, param);
	return true;
}

static void write_sst_file(sst_pf_in_stream *is, level *des){ //for page file
	sst_file *sptr;
	value_set *vs=sst_pis_get_result(is, &sptr);
	sptr->file_addr.map_ppa=page_manager_get_new_ppa(LSM.pm,true,MAPSEG);
	validate_map_ppa(LSM.pm->bm, sptr->file_addr.map_ppa, sptr->start_lba, sptr->end_lba,true);
	
	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=MAPPINGW;
	write_req->param=(void*)vs;
	write_req->end_req=comp_alreq_end_req;

	io_manager_issue_internal_write(sptr->file_addr.map_ppa, vs, write_req, false);
	sptr->data=NULL;
	level_append_sstfile(des, sptr, true);
	sst_free(sptr, LSM.pm);
}

static bool leveling_invalidation_function(level *des, uint32_t stream_id, uint32_t version, 
		key_ptr_pair kp, bool overlap){
	if(overlap){
		if(slm_invalidate_enable(des->idx, kp.piece_ppa)){
			invalidate_piece_ppa(LSM.pm->bm, kp.piece_ppa, true);
			return false;
		}
	}
	else{
		uint32_t lba_version=version_map_lba(LSM.last_run_version, kp.lba);
		if(lba_version!=UINT8_MAX && lba_version > version+1){
			if(stream_id==0 && des->idx!=0 && slm_invalidate_enable(des->idx-1, kp.piece_ppa)){
				invalidate_piece_ppa(LSM.pm->bm, kp.piece_ppa, true);
			}
			else if(stream_id==1 && slm_invalidate_enable(des->idx, kp.piece_ppa)){
				invalidate_piece_ppa(LSM.pm->bm, kp.piece_ppa , true);
			}
			return false;
		}
	}
	return true;
}

uint32_t stream_sorting(level *des, uint32_t stream_num, sst_pf_out_stream **os_set, 
		sst_pf_in_stream *is, std::queue<key_ptr_pair> *kpq, 
		bool all_empty_stop, uint32_t limit, uint32_t target_version,
		bool (*invalidate_function)(level *des, uint32_t stream_id, uint32_t target_version, key_ptr_pair kp, bool overlap)){
	bool one_empty=false;
	if(stream_num >= 32){
		EPRINT("too many stream!!", true);
	}
	
	uint32_t all_empty_check=0;
	uint32_t sorting_idx=0;
	//uint32_t target_version=UINT32_MAX;

	while(!((all_empty_stop && all_empty_check==((1<<stream_num)-1)) || (!all_empty_stop && one_empty))){
		key_ptr_pair target_pair;
		target_pair.lba=UINT32_MAX;
		uint32_t target_idx=UINT32_MAX;
		for(uint32_t i=0; i<stream_num; i++){
			if(all_empty_check & 1<<i) continue;
			if(!os_set[i]){
				all_empty_check|=(1<<i);
				continue;
			}
			key_ptr_pair now=sst_pos_pick(os_set[i]);
			
			if(target_idx==UINT32_MAX){
				 target_pair=now;
				 target_idx=i;
			}
			else{
				if(target_pair.lba > now.lba){
					target_pair=now;
					target_idx=i;
				}
				else if(target_pair.lba!=UINT32_MAX && target_pair.lba==now.lba){
					if(is && !merge_flag){
						if(invalidate_function){
							invalidate_function(des, i, target_version, now, true);
						}
					}
					else if(merge_flag){
						invalidate_function(des, i, os_set[i]->version_idx, now, true);
					}
					else{
						if(kpq){
							invalidate_function(des, i, target_version, now, true);
						}
						else{
							invalidate_function(des, i, os_set[i]->version_idx, now, true);
						}
					}

					sst_pos_pop(os_set[i]);
					continue;
				}
				else{
					continue;
				}
			}
		}
		
	//	if(des && des->idx)

		if((!all_empty_stop) && target_pair.lba>limit){
			break;
		}

		if(target_pair.lba!=UINT32_MAX){
			/*
			if(!merge_flag){
				compaction_debug_func(target_pair.lba, target_pair.piece_ppa, target_version, des);
			}*/
			if(kpq){
				if(invalidate_function && 
						invalidate_function(des, target_idx, os_set[target_idx]->version_idx, target_pair, false)){
					sorting_idx++;
					kpq->push(target_pair);
				}
			}
			else if(is){
				if(invalidate_function && 
						invalidate_function(des, target_idx, target_version, target_pair, false)){
					version_coupling_lba_version(LSM.last_run_version, target_pair.lba, target_version);	
					sorting_idx++;
					if(sst_pis_insert(is, target_pair)){
						write_sst_file(is, des);
					}
				}
			}else{
				EPRINT("plz set one of two(kpq or is)", true);
			}

			sst_pos_pop(os_set[target_idx]);
		}
		if(sst_pos_is_empty(os_set[target_idx])){
			one_empty=true;
			all_empty_check|=1<<target_idx;
		}

	}


	if(is && all_empty_stop && sst_pis_remain_data(is)){
		write_sst_file(is,des);
	}
	return sorting_idx;
}

static sst_file *key_ptr_to_sst_file(key_ptr_pair *kp_set, bool should_flush){
	uint32_t map_ppa=UINT32_MAX;
	uint32_t end_idx=kp_end_idx((char*)kp_set);
	if(should_flush){
		value_set *vs=inf_get_valueset((char*)kp_set, FS_MALLOC_W, PAGESIZE);
		map_ppa=page_manager_get_new_ppa(LSM.pm, false, DATASEG); //DATASEG for sequential tiering 
		validate_map_ppa(LSM.pm->bm, map_ppa, kp_set[0].lba,  kp_set[end_idx].lba, true);
		algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
		write_req->type=MAPPINGW;
		write_req->param=(void*)vs;
		write_req->end_req=comp_alreq_end_req;
		io_manager_issue_internal_write(map_ppa, vs, write_req, false);
	}
	sst_file *sstfile=sst_pf_init(map_ppa, kp_set[0].lba, kp_set[end_idx].lba);
	if(SEGNUM(kp_set[0].piece_ppa)==SEGNUM(kp_set[end_idx].piece_ppa) && SEGNUM(kp_set[0].piece_ppa)==map_ppa/_PPS){
		sstfile->sequential_file=true;
	}

	return sstfile;
}

static void leveling_trivial_move(key_ptr_pair *kp_set,level *up, level *down, level *des, uint32_t to_ridx){
	uint32_t ridx, sidx;
	LSM.monitor.trivial_move_cnt++;
	run *rptr; sst_file *sptr;
	if(kp_set){ // L0
		sst_file *file=key_ptr_to_sst_file(kp_set, true);
		file->_read_helper=read_helper_kpset_to_rh(
				lsmtree_get_target_rhp(des->idx), 
				kp_set);
		if(down->now_sst_num==0){
			level_append_sstfile(des, file, true);
		}
		else{
			if(LAST_RUN_PTR(down)->end_lba < file->start_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr, true);
				}
			}
			level_append_sstfile(des,file, true);
			if(FIRST_RUN_PTR(down)->start_lba > file->end_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr, true);
				}
			}
		}

		for(uint32_t i=0; i<KP_IN_PAGE && kp_set[i].lba!=UINT32_MAX; i++){
			version_coupling_lba_version(LSM.last_run_version, kp_set[i].lba, to_ridx);
		}
		sst_free(file, LSM.pm);
	}
	else{ //L1~LN-1
		if(down->now_sst_num==0){
			for_each_sst_level(up, rptr, ridx, sptr, sidx){
				level_append_sstfile(des, sptr, true);
			}
		}
		else{
			if(LAST_RUN_PTR(down)->end_lba < FIRST_RUN_PTR(up)->start_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr, true);
				}
			}

			for_each_sst_level(up, rptr, ridx, sptr, sidx){
				level_append_sstfile(des, sptr, true);
			}

			if(FIRST_RUN_PTR(down)->start_lba > LAST_RUN_PTR(up)->end_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr, true);
				}
			}
		}

		version_update_for_trivial_move(LSM.last_run_version, FIRST_RUN_PTR(up)->start_lba, 
				LAST_RUN_PTR(up)->end_lba, up->idx, to_ridx);
	}
}

static void compaction_move_unoverlapped_sst
(key_ptr_pair* kp_set, level *up, uint32_t up_start_sidx, 
 level *down, level *des, uint32_t* start_idx){
	bool is_close_target=false;
	uint32_t target_start_lba=kp_set?kp_set[0].lba:(LEVELING_SST_AT_PTR(up, up_start_sidx))->start_lba;
	sst_file *start_sst_file=level_retrieve_sst(down, target_start_lba);
	if(!start_sst_file){
		is_close_target=true;
		start_sst_file=level_retrieve_close_sst(down, target_start_lba);
		if(!start_sst_file){
			*start_idx=0;
			return;
		}
	}

	uint32_t _start_idx=GET_SST_IDX(down, start_sst_file);
	for(uint32_t i=0; i<_start_idx; i++){
		sst_file *sptr=LEVELING_SST_AT_PTR(down, i);
		level_append_sstfile(des, sptr, true);

		version_update_for_trivial_move(LSM.last_run_version, 
				sptr->start_lba, 
				sptr->end_lba,
				up? version_level_to_start_version(LSM.last_run_version, up->idx):UINT32_MAX,
				version_level_to_start_version(LSM.last_run_version, down->idx));
	}
	if(is_close_target){
		sst_file *sptr=LEVELING_SST_AT_PTR(down, _start_idx);
		level_append_sstfile(des, sptr, true);
		version_update_for_trivial_move(LSM.last_run_version, 
				sptr->start_lba, 
				sptr->end_lba,
				up?version_level_to_start_version(LSM.last_run_version, up->idx):UINT32_MAX,
				version_level_to_start_version(LSM.last_run_version, down->idx));
		_start_idx++;
	}

	*start_idx=_start_idx;
}


static void read_params_setting(read_issue_arg *read_arg1, read_issue_arg *read_arg2, level *src, 
		level *des, sst_file *sptr, uint32_t* sidx, uint32_t des_start_idx, 
		uint32_t* des_end_idx, uint32_t now_remain_tags){
	read_arg1->from=*sidx;
	read_arg2->from=des_start_idx;
	uint32_t round=*sidx;
	do{
		sst_file *down_target=level_retrieve_sst(des, sptr->end_lba);
		if(!down_target){
			down_target=level_retrieve_close_sst(des, sptr->end_lba);
		}
		if((round-read_arg1->from+1)+(GET_SST_IDX(des,down_target)-read_arg2->from+1) < now_remain_tags){
			read_arg1->to=round;
			read_arg2->to=GET_SST_IDX(des, down_target);
			*des_end_idx=read_arg2->to;

			(*sidx)++;
			if(!(*sidx<src->now_sst_num)){
				(*sidx)--;
				break;
			}
			sptr=LEVELING_SST_AT_PTR(src, (*sidx));
			round++;
		}
		else{
			break;
		}
	}while(1);

	read_param_init(read_arg1);
	read_param_init(read_arg2);
}

level* compaction_LW2LW(compaction_master *cm, level *src, level *des, uint32_t target_version){
	static uint32_t debug_cnt_flag=0;
	_cm=cm;
	level *res=level_init(des->max_sst_num, des->run_num, des->level_type, des->idx);

	if(!level_check_overlap(src, des)){
		leveling_trivial_move(NULL,src, des, res, target_version);
		return res;
	}


	read_issue_arg read_arg1, read_arg2;
	read_arg1.des=src;
	read_arg2.des=des;

	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*COMPACTION_LEVEL_NUM);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=COMPACTION_LEVEL_NUM;

	sst_pf_out_stream *os_set[COMPACTION_LEVEL_NUM];
	sst_pf_in_stream *is=sst_pis_init(true, 
			lsmtree_get_target_rhp(des->idx)
			);
	
	uint32_t des_start_idx=0;
	uint32_t des_end_idx=0;
	compaction_move_unoverlapped_sst(NULL, src, 0, des, res, &des_start_idx);

	uint32_t target_map_page_num=src->now_sst_num+(des->now_sst_num-des_start_idx);
	if(page_manager_is_gc_needed(LSM.pm, target_map_page_num, true)){
		__do_gc(LSM.pm, true, target_map_page_num);
	}

	run *rptr; sst_file *sptr;
	uint32_t ridx, sidx;
	bool isstart=true;
	uint32_t stream_cnt=0;
	for_each_sst_level(src, rptr, ridx, sptr, sidx){
		/*set read_arg start*/

		read_params_setting(&read_arg1, &read_arg2, src, des, sptr, &sidx, des_start_idx, 
				&des_end_idx, compaction_read_param_remain_num(_cm));
		sidx=read_arg1.to;

		if(isstart){
			os_set[0]=sst_pos_init_sst(LEVELING_SST_AT_PTR(src, read_arg1.from), read_arg1.param, 
					read_arg1.to-read_arg1.from+1, read_done_check, file_done);
			os_set[1]=sst_pos_init_sst(LEVELING_SST_AT_PTR(des, read_arg2.from), read_arg2.param, 
					read_arg2.to-read_arg2.from+1, read_done_check, file_done);
		}
		else{
			sst_pos_add_sst(os_set[0], LEVELING_SST_AT_PTR(src, read_arg1.from), read_arg1.param,
					read_arg1.to-read_arg1.from+1);
			sst_pos_add_sst(os_set[1], LEVELING_SST_AT_PTR(des, read_arg2.from), read_arg2.param,
					read_arg2.to-read_arg2.from+1);
		}

			/*
			printf("%u ~ %u && %u~%u\n",
					LEVELING_SST_AT(src,read_arg1.from).start_lba,
					LEVELING_SST_AT(src,read_arg1.to).end_lba,
					LEVELING_SST_AT(des,read_arg2.from).start_lba,
					LEVELING_SST_AT(des,read_arg2.to).end_lba
					);	
			*/

		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, COMPACTION_LEVEL_NUM, os_set, is, NULL, sidx==src->now_sst_num-1, 
				MIN(LEVELING_SST_AT(src,read_arg1.to).end_lba, LEVELING_SST_AT(des,read_arg2.to).end_lba),
				version_level_to_start_version(LSM.last_run_version, res->idx),
				false,
				leveling_invalidation_function
				);

		isstart=false;
		des_start_idx=des_end_idx+1;
		stream_cnt++;
	}

	for(uint32_t i=des_start_idx; i<des->now_sst_num; i++){
		level_append_sstfile(res, LEVELING_SST_AT_PTR(des, i), true);
	}

	thpool_wait(cm->issue_worker);
	sst_pos_free(os_set[0]);
	sst_pos_free(os_set[1]);

	sst_pis_free(is);

	/*level error check*/
	
	compaction_error_check(NULL, src, des, res, debug_cnt_flag++);
	free(thread_arg.arg_set);
	_cm=NULL;
	return res;

}

int issue_read_kv_for_bos(sst_bf_out_stream *bos, sst_pf_out_stream *pos, 
		uint32_t target_num, uint32_t version, bool round_final){
	key_value_wrapper *read_target;
	uint32_t res=0;
	for(uint32_t i=0; i<target_num  && !sst_pos_is_empty(pos); i++){
		key_ptr_pair target_pair=sst_pos_pick(pos);
		if(target_pair.lba==UINT32_MAX) continue;
		key_value_wrapper *kv_wrapper=(key_value_wrapper*)calloc(1,sizeof(key_value_wrapper));

		kv_wrapper->piece_ppa=target_pair.piece_ppa;
		kv_wrapper->kv_ptr.lba=target_pair.lba;

		if(slm_invalidate_enable(LSM.param.LEVELN-(1+1), kv_wrapper->piece_ppa)){
			invalidate_piece_ppa(LSM.pm->bm, kv_wrapper->piece_ppa, true);
		}

		if(version_map_lba(LSM.last_run_version, target_pair.lba) > version+1){
			/*	
				above level
				LSM.param.LEVELN-(1+1) (last leveling)
				LSM.param.LEVELN-1 (tierine)
			 */
			free(kv_wrapper);
			sst_pos_pop(pos);
			continue;
		}

		if((read_target=sst_bos_add(bos, kv_wrapper, _cm))){
			if(!read_target->param){
				EPRINT("can't be",true);
			}
			algo_req *read_req=(algo_req*)malloc(sizeof(algo_req));
			read_req->type=COMPACTIONDATAR;
			read_req->param=(void*)read_target;
			read_req->end_req=comp_alreq_end_req;
			io_manager_issue_read(PIECETOPPA(read_target->piece_ppa),
					read_target->param->data, read_req, false);
		}

		sst_pos_pop(pos);
		res++;
	}

	if(sst_pos_is_empty(pos) && round_final){
		if((read_target=sst_bos_get_pending(bos, _cm))){
			algo_req *read_req=(algo_req*)malloc(sizeof(algo_req));
			read_req->type=COMPACTIONDATAR;
			read_req->param=(void*)read_target;
			read_req->end_req=comp_alreq_end_req;		
			io_manager_issue_read(PIECETOPPA(read_target->piece_ppa),
					read_target->param->data, read_req, false);
		}
	}
	return res;
}


 sst_file *bis_to_sst_file(sst_bf_in_stream *bis){
	if(bis->map_data->size()==0) return NULL;
	sst_file *res=sst_init_empty(BLOCK_FILE);
	uint32_t map_num=0;
	uint32_t ppa;
	map_range *mr_set=(map_range*)malloc(sizeof(map_range) * 
			bis->map_data->size());
	uint32_t mr_idx=0;
	while(bis->map_data->size()){
		value_set *data=bis->map_data->front();
		algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
		write_req->type=COMPACTIONDATAW;
		write_req->param=(void*)data;
		write_req->end_req=comp_alreq_end_req;

		map_num++;

		ppa=page_manager_get_new_ppa_from_seg(LSM.pm, bis->seg);
		if(bis->start_piece_ppa/L2PGAP/_PPS!=ppa/_PPS){
			EPRINT("map data should same sgement", true);
		}

		mr_set[mr_idx].start_lba=((key_ptr_pair*)data->value)[0].lba;
		mr_set[mr_idx].end_lba=kp_get_end_lba(data->value);
		mr_set[mr_idx].ppa=ppa;

		//uint32_t temp_ppa=ppa*L2PGAP;
		validate_map_ppa(LSM.pm->bm, ppa, mr_set[mr_idx].start_lba, mr_set[mr_idx].end_lba, true);
		//validate_piece_ppa(LSM.pm->bm,1, &temp_ppa, &mr_set[mr_idx].start_lba);

		io_manager_issue_write(ppa, data, write_req, false);

		mr_idx++;
		bis->map_data->pop();
	}

	sst_set_file_map(res,mr_idx, mr_set);

	res->file_addr.piece_ppa=bis->start_piece_ppa;
	res->end_ppa=ppa;
	res->map_num=map_num;
	res->start_lba=bis->start_lba;
	res->end_lba=bis->end_lba;
	res->_read_helper=bis->rh;
	read_helper_insert_done(bis->rh);
	bis->rh=NULL;
	return res;
}

sst_bf_in_stream *tiering_new_bis(uint32_t level_idx){
	__segment *seg=page_manager_get_seg_for_bis(LSM.pm, DATASEG);
	read_helper_param temp_rhp=lsmtree_get_target_rhp(level_idx);
	temp_rhp.member_num=(_PPS-seg->used_page_num)*L2PGAP;
	sst_bf_in_stream *bis=sst_bis_init(seg, LSM.pm, true, temp_rhp);
	return bis;
}

void issue_bis_result(sst_bf_in_stream *bis, uint32_t target_ridx, bool final){
	uint32_t debug_idx=0;
	key_ptr_pair debug_kp[L2PGAP];
	value_set *result=sst_bis_get_result(bis, final, &debug_idx, debug_kp);
	for(uint32_t i=0; i<debug_idx; i++){
		compaction_debug_func(debug_kp[i].lba, debug_kp[i].piece_ppa, target_ridx,
				LSM.disk[LSM.param.LEVELN-1]);
	}
	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=COMPACTIONDATAW;
	write_req->param=(void*)result;
	write_req->end_req=comp_alreq_end_req;
	io_manager_issue_write(result->ppa, result, write_req, false);	
}

uint32_t issue_write_kv_for_bis(sst_bf_in_stream **bis, sst_bf_out_stream *bos, run *new_run,
		int32_t entry_num, uint32_t target_version, bool final){
	int32_t inserted_entry_num=0;
	uint32_t last_lba=UINT32_MAX;

	fdriver_lock(&LSM.flush_lock);
	uint32_t level_idx=version_to_level_idx(LSM.last_run_version, target_version, LSM.param.LEVELN);
	while(!sst_bos_is_empty(bos)){
		key_value_wrapper *target=NULL;

		if(!final && !(target=sst_bos_pick(bos, entry_num-inserted_entry_num <=L2PGAP))){
			break;
		}
		else if(final){
			target=sst_bos_pick(bos, entry_num-inserted_entry_num<=L2PGAP);
			if(!target){
				target=sst_bos_get_pending(bos, _cm);
			}
		}

		if(target){
			last_lba=target->kv_ptr.lba;
			version_coupling_lba_version(LSM.last_run_version, target->kv_ptr.lba, target_version);
		}

		if((target && sst_bis_insert(*bis, target)) ||
				(final && sst_bos_kv_q_size(bos)==1)){
			issue_bis_result(*bis, target_version, final);
		}

		sst_bos_pop(bos, _cm);

		if(sst_bis_ppa_empty(*bis)){
			sst_file *sptr=bis_to_sst_file(*bis);
			lsmtree_gc_unavailable_set(&LSM, sptr, UINT32_MAX);
			run_append_sstfile_move_originality(new_run, sptr);
			sst_free(sptr, LSM.pm);
			sst_bis_free(*bis);
			*bis=tiering_new_bis(level_idx);
		}
		inserted_entry_num++;
	}
	
	if(final && sst_bos_kv_q_size(bos)==0 && (*bis)->buffer_idx){
		issue_bis_result(*bis, target_version, final);
	}
	fdriver_unlock(&LSM.flush_lock);
	return last_lba;
}

static inline run *filter_sequential_file(level *src, uint32_t max_sst_file_num, 
		uint32_t target_version, uint32_t* start_sst_file_idx, uint32_t des_idx){
	run *new_run=run_init(max_sst_file_num, UINT32_MAX, 0);
	bool moved=false;
	run *rptr;
	sst_file *sptr;
	sst_file *prev_sptr=NULL;
	uint32_t ridx, sidx;
	sst_queue *pf_queue=NULL;
	for_each_sst_level(src, rptr, ridx, sptr, sidx){
		if(!sptr->sequential_file) break;
		moved=true;
		if(prev_sptr==NULL){
			prev_sptr=sptr;
			pf_queue=new sst_queue();
			pf_queue->push(sptr);
		}
		else{
			if(prev_sptr->file_addr.map_ppa/_PPS == sptr->file_addr.map_ppa/_PPS){
				pf_queue->push(sptr);
			}
			else{
				sst_file *block_file=compaction_seq_pagesst_to_blocksst(pf_queue, des_idx);
				run_append_sstfile_move_originality(new_run, block_file);
				sst_free(block_file, LSM.pm);
				delete pf_queue;
				pf_queue=new sst_queue();
				pf_queue->push(sptr);
			}
			prev_sptr=sptr;
		}
		//version update!
		version_update_for_trivial_move(LSM.last_run_version, 
				sptr->start_lba, 
				sptr->end_lba,
				version_level_to_start_version(LSM.last_run_version, src->idx),
				target_version);
	}

	if(pf_queue){
		sst_file *block_file=compaction_seq_pagesst_to_blocksst(pf_queue, des_idx);
		run_append_sstfile_move_originality(new_run, block_file);
		sst_free(block_file, LSM.pm);
		delete pf_queue;
	}

	if(moved){
		*start_sst_file_idx=sidx;
	}
	return new_run;
}

run *compaction_wisckey_to_normal(compaction_master *cm, level *src, 
		run *previous_run, uint32_t *moved_entry_num, 
		uint32_t start_sst_file_idx, uint32_t target_version){
	_cm=cm;
	run *new_run=previous_run?previous_run:run_init(src->now_sst_num, UINT32_MAX, 0);
	read_issue_arg read_arg;
	read_arg.des=src;
	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg));
	thread_arg.arg_set[0]=&read_arg;
	thread_arg.set_num=1;

	sst_pf_out_stream *pos=NULL;
	sst_bf_out_stream *bos=sst_bos_init(read_done_check, false);
	sst_bf_in_stream *bis=tiering_new_bis(src->idx+1);

	uint32_t total_num=(start_sst_file_idx==UINT32_MAX?src->now_sst_num:src->now_sst_num-(start_sst_file_idx+1));
	start_sst_file_idx=(start_sst_file_idx==UINT32_MAX?0:start_sst_file_idx);

	uint32_t start_idx=start_sst_file_idx;
	uint32_t tier_compaction_tags=COMPACTION_TAGS/COMPACTION_LEVEL_NUM;
	uint32_t round=total_num/tier_compaction_tags+(total_num%tier_compaction_tags?1:0);

	uint32_t total_moved_num=0;

	uint32_t j=0;
	bool temp_final=false;
	uint32_t needed_seg_num=0;
	for(uint32_t i=0; i<round; i++){
		read_arg.from=start_idx+i*tier_compaction_tags;
		if(i!=round-1){
			read_arg.to=start_idx+(i+1)*tier_compaction_tags-1;
		}
		else{
			read_arg.to=src->now_sst_num-1;	
		}
		read_param_init(&read_arg);

		needed_seg_num=(TARGETREADNUM(read_arg)*KP_IN_PAGE/L2PGAP+TARGETREADNUM(read_arg))/_PPS+1+1;
		if(page_manager_get_total_remain_page(LSM.pm, false) <needed_seg_num * _PPS){ 
			__do_gc(LSM.pm, false, needed_seg_num*_PPS);
		}

		if(i==0){
			pos=sst_pos_init_sst(LEVELING_SST_AT_PTR(src, read_arg.from), read_arg.param, 
					read_arg.to-read_arg.from+1, read_done_check, file_done);
		}
		else{
			sst_pos_add_sst(pos, LEVELING_SST_AT_PTR(src, read_arg.from), read_arg.param,
					read_arg.to-read_arg.from+1);
		}

		read_sst_job((void*)&thread_arg,-1);
		
		uint32_t round2_tier_compaction_tags, picked_kv_num;

		do{ 
			round2_tier_compaction_tags=cm->read_param_queue->size();
			picked_kv_num=issue_read_kv_for_bos(bos, pos, round2_tier_compaction_tags, target_version, 
					true);
			LSM.monitor.tiering_valid_entry_cnt+=picked_kv_num;
			round2_tier_compaction_tags=MIN(round2_tier_compaction_tags, picked_kv_num);
			total_moved_num+=picked_kv_num;
			temp_final=((i==round-1 && sst_pos_is_empty(pos)));
			issue_write_kv_for_bis(&bis, bos, new_run, round2_tier_compaction_tags, target_version, 
					temp_final);
			j++;
		}
		while(!temp_final && !sst_pos_is_empty(pos));
	}

	thpool_wait(cm->issue_worker);
	//finishing bis
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

	if(!total_moved_num){
		EPRINT("no data moved", false);
	}

	if(moved_entry_num){
		*moved_entry_num=total_moved_num;
	}

	sst_pos_free(pos);
	sst_bis_free(bis);
	sst_bos_free(bos, _cm);
	free(thread_arg.arg_set);
	return new_run;
}

level* compaction_LW2TI(compaction_master *cm, level *src, level *des, uint32_t target_version){ /*move to last level*/
	_cm=cm;
	uint32_t start_sst_file_idx=UINT32_MAX;
	run *new_run=filter_sequential_file(src, des->max_sst_num/des->max_run_num, target_version, 
			&start_sst_file_idx, des->idx); //version_update
	uint32_t target_run_idx=version_to_ridx(LSM.last_run_version, 
			target_version, des->idx);
	if(start_sst_file_idx!=UINT32_MAX && start_sst_file_idx==src->now_sst_num-1){ //all sequential_file
		level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx);
		level_update_run_at_move_originality(res, target_run_idx, new_run, true);
		run_free(new_run);
		return res;
	}
	
	//level_content_print(LSM.disk[1], true);

	uint32_t needed_seg_num=0;
	if(start_sst_file_idx==UINT32_MAX){
		needed_seg_num=(((src->now_sst_num)*KP_IN_PAGE/L2PGAP+(src->now_sst_num))/_PPS+1+1);
	//	start_sst_file_idx=0;
	}
	else{
		if(src->now_sst_num==start_sst_file_idx){
			level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx);
			run *rptr; uint32_t ridx;
			for_each_run_max(des, rptr, ridx){
				if(rptr->now_sst_file_num){
					level_append_run_copy_move_originality(res, rptr, ridx);
				}
			}
			level_update_run_at_move_originality(res, target_run_idx, new_run, true);
			tiering_compaction_error_check(src, NULL, NULL, new_run, LSM.monitor.compaction_cnt[des->idx]);
			run_free(new_run);
			level_print(res);
			return res;
		}
		else{
			needed_seg_num=(((src->now_sst_num-start_sst_file_idx)*KP_IN_PAGE/L2PGAP+(src->now_sst_num-start_sst_file_idx))/_PPS+1+1);
		}
	}

	uint32_t total_moved_num=0;
	new_run=compaction_wisckey_to_normal(cm, src, new_run, &total_moved_num, start_sst_file_idx, target_version);

	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx);
	run *rptr; uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(rptr->now_sst_file_num){
			level_append_run_copy_move_originality(res, rptr, ridx);
		}
	}

	//level_append_run(res, new_run);
	if(total_moved_num){
		level_update_run_at_move_originality(res, target_run_idx, new_run, true);
		/*
		if(level_is_full(res)){
			version_make_early_invalidation_enable_old(LSM.last_run_version);
		}*/
	}
	else{
		EPRINT("no data move", true);
	}

	sst_file *temp_sptr;
	uint32_t temp_sidx;
	for_each_sst(new_run, temp_sptr, temp_sidx){
		if(!temp_sptr->sequential_file){
			lsmtree_gc_unavailable_unset(&LSM, temp_sptr, UINT32_MAX);
		}
	}
	run_free(new_run);
	level_print(res);
//	lsmtree_gc_unavailable_sanity_check(&LSM);
	/*finish logic*/
	return res;
}

/*
   This function is tightly coupled with compaction_LE2LE()
   If someone fixs this code, please check compaction_LE2LE() code.
 */
level* compaction_LW2LE(compaction_master *cm, level *src, level *des, uint32_t target_version){
	_cm=cm;
	if(!level_check_overlap(src, des)){ //sequential
		run *new_run=compaction_wisckey_to_normal(cm, src, NULL, NULL, UINT32_MAX, target_version);
		level *res=level_init(des->max_sst_num, des->run_num, des->level_type, des->idx);
		sst_file *sptr;
		run *rptr; 
		uint32_t sidx, ridx;
		bool move=false;
		if(LAST_RUN_PTR(src)->end_lba < FIRST_RUN_PTR(des)->start_lba){
			move=true;
			for_each_sst(new_run, sptr, sidx){
				level_append_sstfile(res, sptr, true);
			}
		}

		for_each_sst_level(des, rptr, ridx, sptr, sidx){
			level_append_sstfile(res, sptr, true);
		}	

		if(!move){
			for_each_sst(new_run, sptr, sidx){
				level_append_sstfile(res, sptr, true);
			}	
		}
		run_free(new_run);
		/*insert keeping order into level*/
		return res;
	}

	uint32_t closed_upper_from=0, closed_upper_to=0;

	level *res=level_init(des->max_sst_num, des->run_num, des->level_type, des->idx);
	/*move unoverlapped sst of above level*/
	bool trivial_move=leveling_get_sst_overlap_range(src, des, &closed_upper_from, &closed_upper_to);
	run *new_run=NULL;
	if(trivial_move){
		level *temp_level=level_split_lw_run_to_lev(&src->array[0], 0, closed_upper_from-1);
		new_run=compaction_wisckey_to_normal(cm, temp_level, NULL, NULL, closed_upper_from, target_version);
		sst_file *sptr;
		uint32_t sidx;
		for_each_sst(new_run, sptr, sidx){
			level_append_sstfile(res, sptr, true);
		}
		run_free(new_run);
		level_free(temp_level, LSM.pm);
	}

	new_run=run_init(des->now_sst_num+src->now_sst_num, UINT32_MAX, 0);

	uint32_t upper_version=version_pick_oldest_version(LSM.last_run_version, src->idx);
	uint32_t lower_version=version_pick_oldest_version(LSM.last_run_version, des->idx);

	uint32_t des_start_idx=0;
	uint32_t des_end_idx=0;
	/*move unoverlapped sst of below level*/
	compaction_move_unoverlapped_sst(NULL, src, closed_upper_from, des, res, &des_start_idx);

	read_issue_arg read_arg1={0,}, read_arg2={0,};
	read_arg1.des=src;

	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*MERGED_RUN_NUM);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=COMPACTION_LEVEL_NUM;

	sst_pf_out_stream *os_set[COMPACTION_LEVEL_NUM];
	sst_bf_out_stream *bos=NULL;
	sst_bf_in_stream *bis=NULL;
	std::queue<key_ptr_pair> *kpq=new std::queue<key_ptr_pair>();

	sst_file *sptr; run *rptr;
	uint32_t ridx, sidx=closed_upper_from;
	level *temp_des_level=level_convert_normal_run_to_LW(&des->array[0], LSM.pm, des_start_idx, 
			des->now_sst_num-1);
	read_arg2.des=temp_des_level;

	bool isstart=true;
	uint32_t stream_cnt=0;
	for_each_sst_level_at(src, rptr, ridx, sptr, sidx){
		read_params_setting(&read_arg1, &read_arg2, src, temp_des_level, sptr, &sidx, 
				des_start_idx, &des_end_idx, compaction_read_param_remain_num(_cm));
		sidx=read_arg1.to;
		bool final_flag=sidx==src->now_sst_num-1;

		if(isstart){
			os_set[0]=sst_pos_init_sst(LEVELING_SST_AT_PTR(src, read_arg1.from), read_arg1.param, 
					read_arg1.to-read_arg1.from+1, read_done_check, file_done);
			os_set[1]=sst_pos_init_sst(LEVELING_SST_AT_PTR(temp_des_level, read_arg2.from), read_arg2.param, 
					read_arg2.to-read_arg2.from+1, read_done_check, file_done);
		}
		else{
			sst_pos_add_sst(os_set[0], LEVELING_SST_AT_PTR(src, read_arg1.from), read_arg1.param,
					read_arg1.to-read_arg1.from+1);
			sst_pos_add_sst(os_set[1], LEVELING_SST_AT_PTR(temp_des_level, read_arg2.from), read_arg2.param,
					read_arg2.to-read_arg2.from+1);
		}

		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, COMPACTION_LEVEL_NUM, os_set, NULL, kpq, final_flag, 
				MIN(LEVELING_SST_AT(src,read_arg1.to).end_lba, LEVELING_SST_AT(temp_des_level,read_arg2.to).end_lba),
				version_level_to_start_version(LSM.last_run_version, res->idx),
				false,
				leveling_invalidation_function
				);

		if(bos==NULL){
			bos=sst_bos_init(read_map_done_check, true);
		}
		if(bis==NULL){
			bis=tiering_new_bis(des->idx);
		}

		uint32_t entry_num=issue_read_kv_for_bos_normal(bos, kpq, final_flag,
				upper_version, lower_version);

		uint32_t needed_seg_num=(entry_num/L2PGAP+1)/_PPS+1;
		if(page_manager_get_total_remain_page(LSM.pm, false) < needed_seg_num * _PPS){
			__do_gc(LSM.pm, false, needed_seg_num *_PPS);
		}
		
		//dogc

		issue_write_kv_for_bis(&bis, bos, new_run, entry_num, lower_version, final_flag);
		isstart=false;
		des_start_idx=des_end_idx+1;
		stream_cnt++;
	}

	sst_file *last_file;
	if((last_file=bis_to_sst_file(bis))){
		run_append_sstfile_move_originality(new_run, last_file);
		sst_free(last_file, LSM.pm);
	}

	if(bis->seg->used_page_num!=_PPS){
		if(LSM.pm->temp_data_segment){
			EPRINT("should be NULL", true);
		}
		LSM.pm->temp_data_segment=bis->seg;
	}

	for_each_sst(new_run, sptr, sidx){
		level_append_sstfile(res, sptr, true);
	}
	run_free(new_run);

	level_free(temp_des_level, LSM.pm);
	sst_bis_free(bis);
	sst_bos_free(bos, _cm);
	sst_pos_free(os_set[0]);
	sst_pos_free(os_set[1]);
	delete kpq;
	free(thread_arg.arg_set);
	return res;
}


level* compaction_LE2LE(compaction_master *cm, level *src, level *des, uint32_t target_version){
	_cm=cm;
	if(!level_check_overlap(src, des)){
		level *res=level_init(des->max_sst_num, des->run_num, des->level_type, des->idx);
		sst_file *sptr;
		run *rptr; 
		uint32_t sidx, ridx;
		bool move=false;
		if(LAST_RUN_PTR(src)->end_lba < FIRST_RUN_PTR(des)->start_lba){
			for_each_sst_level(src, rptr, ridx, sptr, sidx){
				level_append_sstfile(res, sptr, true);
			}
			move=true;
		}

		for_each_sst_level(des, rptr, ridx, sptr, sidx){
			level_append_sstfile(res, sptr, true);
		}	

		if(!move){
			for_each_sst_level(src, rptr, ridx, sptr, sidx){
				level_append_sstfile(res, sptr, true);
			}	
		}
		/*insert keeping order into level*/
		return res;
	}

	uint32_t closed_upper_from=0, closed_upper_to=0;

	uint32_t upper_version=version_pick_oldest_version(LSM.last_run_version, src->idx);
	uint32_t lower_version=version_pick_oldest_version(LSM.last_run_version, des->idx);

	level *res=level_init(des->max_sst_num, des->run_num, des->level_type, des->idx);
	/*move unoverlapped sst of above level*/
	bool trivial_move=leveling_get_sst_overlap_range(src, des, &closed_upper_from, &closed_upper_to);
	if(trivial_move){
		level *temp_level=level_split_lw_run_to_lev(&src->array[0], 0, closed_upper_from-1);
		run *rptr;
		sst_file *sptr;
		uint32_t sidx, ridx;
		for_each_sst_level(temp_level, rptr, ridx, sptr, sidx){
			level_append_sstfile(res, sptr, true);
		}
		level_free(temp_level, LSM.pm);
	}

	uint32_t des_start_idx=0, des_end_idx;
	/*move unoverlapped sst of below level*/
	compaction_move_unoverlapped_sst(NULL, src, closed_upper_from, des, res, &des_start_idx);

	read_issue_arg read_arg1={0,}, read_arg2={0,};
	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*MERGED_RUN_NUM);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=COMPACTION_LEVEL_NUM;

	sst_pf_out_stream *os_set[COMPACTION_LEVEL_NUM];
	sst_bf_out_stream *bos=NULL;
	sst_bf_in_stream *bis=NULL;
	std::queue<key_ptr_pair> *kpq=new std::queue<key_ptr_pair>();

	sst_file *sptr; run *rptr;
	uint32_t ridx, sidx=closed_upper_from;
	level *temp_src_level=level_convert_normal_run_to_LW(&src->array[0], LSM.pm, closed_upper_from, 
			src->now_sst_num-1);
	read_arg1.des=temp_src_level;
	level *temp_des_level=level_convert_normal_run_to_LW(&des->array[0], LSM.pm, des_start_idx, 
			des->now_sst_num-1);
	read_arg2.des=temp_des_level;

	run *new_run=run_init(des->now_sst_num+src->now_sst_num, UINT32_MAX, 0);

	bool isstart=true;
	uint32_t stream_cnt=0;
	for_each_sst_level_at(temp_src_level, rptr, ridx, sptr, sidx){
		read_params_setting(&read_arg1, &read_arg2, temp_src_level, temp_des_level, sptr, &sidx, 
				des_start_idx, &des_end_idx, compaction_read_param_remain_num(_cm));
		sidx=read_arg1.to;
		bool final_flag=sidx==temp_src_level->now_sst_num-1;

		if(isstart){
			os_set[0]=sst_pos_init_sst(LEVELING_SST_AT_PTR(temp_src_level, read_arg1.from), read_arg1.param, 
					read_arg1.to-read_arg1.from+1, read_done_check, file_done);
			os_set[1]=sst_pos_init_sst(LEVELING_SST_AT_PTR(temp_des_level, read_arg2.from), read_arg2.param, 
					read_arg2.to-read_arg2.from+1, read_done_check, file_done);
		}
		else{
			sst_pos_add_sst(os_set[0], LEVELING_SST_AT_PTR(temp_src_level, read_arg1.from), read_arg1.param,
					read_arg1.to-read_arg1.from+1);
			sst_pos_add_sst(os_set[1], LEVELING_SST_AT_PTR(temp_des_level, read_arg2.from), read_arg2.param,
					read_arg2.to-read_arg2.from+1);
		}

		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, COMPACTION_LEVEL_NUM, os_set, NULL, kpq, final_flag, 
				MIN(LEVELING_SST_AT(temp_src_level,read_arg1.to).end_lba, 
					LEVELING_SST_AT(temp_des_level,read_arg2.to).end_lba),
				version_level_to_start_version(LSM.last_run_version, res->idx),
				false,
				leveling_invalidation_function
				);

		if(bos==NULL){
			bos=sst_bos_init(read_map_done_check, true);
		}
		if(bis==NULL){
			bis=tiering_new_bis(des->idx);
		}

		uint32_t entry_num=issue_read_kv_for_bos_normal(bos, kpq, final_flag,
				upper_version, lower_version);

		uint32_t needed_seg_num=(entry_num/L2PGAP+1)/_PPS+1;
		if(page_manager_get_total_remain_page(LSM.pm, false) < needed_seg_num * _PPS){
			__do_gc(LSM.pm, false, needed_seg_num *_PPS);
		}
		
		//dogc

		issue_write_kv_for_bis(&bis, bos, new_run, entry_num, lower_version, final_flag);
		isstart=false;
		des_start_idx=des_end_idx+1;
		stream_cnt++;
	}

	sst_file *last_file;
	if((last_file=bis_to_sst_file(bis))){
		run_append_sstfile_move_originality(new_run, last_file);
		sst_free(last_file, LSM.pm);
	}

	if(bis->seg->used_page_num!=_PPS){
		if(LSM.pm->temp_data_segment){
			EPRINT("should be NULL", true);
		}
		LSM.pm->temp_data_segment=bis->seg;
	}

	for_each_sst(new_run, sptr, sidx){
		level_append_sstfile(res, sptr, true);
	}
	run_free(new_run);

	level_free(temp_des_level, LSM.pm);
	level_free(temp_src_level, LSM.pm);
	sst_bis_free(bis);
	sst_bos_free(bos, _cm);
	sst_pos_free(os_set[0]);
	sst_pos_free(os_set[1]);
	delete kpq;
	free(thread_arg.arg_set);
	return res;
		
}

/*
	This function returnsa  new level by merging inserted two levels by the tiered compaction manner.
	Since the above level should be leveled level, it does not need to sort the above level.
	In this function, the above level just moves to the next level's empty run.
	There is no special logic for sequential workload.
 */
level *compaction_LE2TI(compaction_master *cm, level *src, level *des, uint32_t target_version){
	run *new_run=level_LE_to_run(src, true);
	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx);
	//level_run_reinit(des, idx_set[1]);
	run *rptr;
	uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(rptr->now_sst_file_num){
			level_append_run_copy_move_originality(res, rptr, ridx);
		}
	}

	uint32_t start_lba=src->array[0].start_lba;
	uint32_t end_lba=src->array[0].end_lba;
	version_update_for_trivial_move(LSM.last_run_version, start_lba, end_lba,
			src->idx, target_version);

	uint32_t target_ridx=target_version-version_level_to_start_version(LSM.last_run_version, des->idx);
	level_update_run_at_move_originality(res, target_ridx, new_run, true);
	run_free(new_run);
	return res;
}


level* compaction_LW2TW(compaction_master *cm, level *src, level *des, uint32_t target_version){
	run *new_run=level_LE_to_run(src, true);
	level *res=level_init(des->max_sst_num, des->max_run_num, des->level_type, des->idx);
	//level_run_reinit(des, idx_set[1]);
	run *rptr;
	uint32_t ridx;
	for_each_run_max(des, rptr, ridx){
		if(rptr->now_sst_file_num){
			level_append_run_copy_move_originality(res, rptr, ridx);
		}
	}

	uint32_t start_lba=src->array[0].start_lba;
	uint32_t end_lba=src->array[0].end_lba;
	version_update_for_trivial_move(LSM.last_run_version, start_lba, end_lba,
			src->idx, target_version);

	uint32_t target_ridx=target_version-version_level_to_start_version(LSM.last_run_version, des->idx);
	level_update_run_at_move_originality(res, target_ridx, new_run, true);
	run_free(new_run);
	return res;
}

void *comp_alreq_end_req(algo_req *req){
	inter_read_alreq_param *r_param;
	value_set *vs;
	key_value_wrapper *kv_wrapper;
	switch(req->type){
		case MAPPINGW:
			vs=(value_set*)req->param;
			inf_free_valueset(vs, FS_MALLOC_W);
			break;
		case MAPPINGR:
			r_param=(inter_read_alreq_param*)req->param;
			fdriver_unlock(&r_param->done_lock);
			break;
		case COMPACTIONDATAR:
		case DATAR:
			kv_wrapper=(key_value_wrapper*)req->param;
			fdriver_unlock(&kv_wrapper->param->done_lock);
			break;
		case COMPACTIONDATAW:
		case DATAW:
			vs=(value_set*)req->param;
			inf_free_valueset(vs, FS_MALLOC_W);
			break;
	}
	free(req);
	return NULL;
}

#if 0
level* compaction_first_leveling(compaction_master *cm, key_ptr_pair *kp_set, level *des){
	static int debug_cnt_flag=0;
	_cm=cm;
	level *res=level_init(des->max_sst_num, des->run_num, des->level_type, des->idx);

	if(!level_check_overlap_keyrange(kp_set[0].lba, kp_get_end_lba((char*)kp_set), des)){

		leveling_trivial_move(kp_set,NULL,  des, res, 
				version_level_to_start_version(LSM.last_run_version, des->idx));

		return res;
	}
	LSM.monitor.compaction_cnt[0]++;

	if(page_manager_is_gc_needed(LSM.pm, des->now_sst_num+1, true)){
		__do_gc(LSM.pm, true, des->now_sst_num+1);
	}

	/*each round read/write 128 data*/
	/*we need to have rate limiter!!*/
	read_issue_arg read_arg;
	read_arg.des=des;
	read_arg_container thread_arg;
	thread_arg.end_req=comp_alreq_end_req;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg));
	thread_arg.arg_set[0]=&read_arg;
	thread_arg.set_num=1;

	sst_pf_out_stream *os_set[COMPACTION_LEVEL_NUM]={NULL,};
	sst_pf_out_stream *os;
	sst_pf_in_stream *is=sst_pis_init(true, 
			lsmtree_get_target_rhp(des->idx));

	uint32_t start_idx=0;
	compaction_move_unoverlapped_sst(kp_set, NULL, 0, des, res, &start_idx);
	uint32_t total_num=des->now_sst_num-start_idx+1;
	uint32_t level_compaction_tags=COMPACTION_TAGS/COMPACTION_LEVEL_NUM;
	uint32_t round=total_num/level_compaction_tags+(total_num%level_compaction_tags?1:0);
	for(uint32_t i=0; i<round; i++){
		/*set read_arg*/
		read_arg.from=start_idx+i*level_compaction_tags;
		if(i!=round-1){
			read_arg.to=start_idx+(i+1)*level_compaction_tags-1;
		}
		else{
			read_arg.to=des->now_sst_num-1;	
		}
		read_param_init(&read_arg);

		if(i==0){
			os=sst_pos_init_sst(LEVELING_SST_AT_PTR(des, read_arg.from), read_arg.param, 
					read_arg.to-read_arg.from+1, read_done_check, file_done);
			os_set[0]=sst_pos_init_kp(kp_set);
			os_set[1]=os;
		}
		else{
			sst_pos_add_sst(os_set[1], LEVELING_SST_AT_PTR(des, read_arg.from), read_arg.param,
					read_arg.to-read_arg.from+1);
		}
		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, COMPACTION_LEVEL_NUM, os_set, is, NULL, i==round-1, 
				MIN(LEVELING_SST_AT(des,read_arg.to).end_lba, kp_set[LAST_KP_IDX].lba),
				version_level_to_start_version(LSM.last_run_version, res->idx),
				false,
				leveling_invalidation_function);

	}
	
	thpool_wait(cm->issue_worker);
	sst_pos_free(os_set[0]);
	sst_pos_free(os_set[1]);

	sst_pis_free(is);

	//level_print(res);
	compaction_error_check(kp_set, NULL, des, res, debug_cnt_flag++);

	free(thread_arg.arg_set);
	_cm=NULL;
	return res;
}
#endif
