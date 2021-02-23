#include "compaction.h"
#include "run.h"
#include "../../include/sem_lock.h"
#include "../../interface/interface.h"
#include "sst_set_stream.h"
#include "io.h"
extern lsmtree LSM;
typedef struct{
	level *des;
	uint32_t from;
	uint32_t to;
	inter_read_alreq_param *param;
}read_issue_arg;

typedef struct{
	read_issue_arg **arg_set;
	uint32_t set_num;
}read_arg_container;

void *comp_alreq_end_req(algo_req *req);

static void read_sst_job(void *arg, int th_num){
	read_arg_container *thread_arg=(read_arg_container*)arg;
	read_issue_arg **arg_set=thread_arg->arg_set;
	uint32_t *idx_set=(uint32_t*)calloc(thread_arg->set_num, sizeof(uint32_t));
	inter_read_alreq_param **param_set=(inter_read_alreq_param **)calloc(thread_arg->set_num, sizeof(inter_read_alreq_param*));
	for(uint32_t i=0; i<thread_arg->set_num; i++){
		param_set[i]=arg_set[i]->param;
	}
	uint32_t remain_checker=0, target=(1<<(thread_arg->set_num+1))-1;
	while(!(remain_checker==target)){
		for(uint32_t i=0; i<thread_arg->set_num; i++){
			if(remain_checker&=1<<i) continue;
			inter_read_alreq_param *now=&param_set[i][idx_set[i]++];
			algo_req *read_req=(algo_req*)malloc(sizeof(algo_req));
			//read_req->ppa=now->target->piece_ppa;
			read_req->type=MAPPINGR;
			read_req->param=(void*)now;
			io_manager_issue_read(now->target->ppa, now->data, read_req, false);

			if(idx_set[i]==(arg_set[i]->to-arg_set[i]->from+1)){
				remain_checker|=1<<i;
			}
		}
	}
}

static void read_param_init(inter_read_alreq_param *param, read_issue_arg *read_arg){
	for(int i=0; i<read_arg->from-read_arg->to+1; i++){
		param[i].target=LEVELING_SST_AT_PTR(read_arg->des, read_arg->from+i);
		param[i].data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
		fdriver_lock_init(&param[i].done_lock, 0);
	}
	read_arg->param=param;
}


static void read_param_free(inter_read_alreq_param *param, read_issue_arg *read_arg){
	for(int i=0; i<read_arg->from-read_arg->to+1; i++){
		inf_free_valueset(param[i].data, FS_MALLOC_R);
		fdriver_destroy(&param[i].done_lock);
	}
}

bool read_done_check(void *_param){
	inter_read_alreq_param *param=(inter_read_alreq_param*)_param;
	param->target->data=param->data->value;
	fdriver_lock(&param->done_lock);
	return true;
}

static void stream_sorting(level *des, uint32_t stream_num, sst_out_stream **os_set, 
		sst_in_stream *is, void (*write_function)(sst_in_stream* is,  level*), bool all_empty_stop){
	uint32_t target_idx=UINT32_MAX;
	bool one_empty=false;
	if(stream_num >= 32){
		EPRINT("too many stream!!", true);
	}
	uint32_t all_empty_check=0;
	//sst_file *target_sst_file;
	while(!(all_empty_stop && all_empty_check==(1<<(stream_num-1))) || !(!all_empty_stop && one_empty)){
		key_ptr_pair target_pair;
		for(uint32_t i=0; i<stream_num; i++){
			key_ptr_pair now=sst_os_pick(os_set[i]);
			if(target_idx==UINT32_MAX){
				 target_pair=now;
				 target_idx=i;
			}
			else{
				if(target_pair.lba > now.lba){
					target_pair=now;
					target_idx=i;
				}
				else if(target_pair.lba==now.lba){
					invalidate_piece_ppa(LSM.pm->bm,now.piece_ppa);
					sst_os_pop(os_set[i]);
					continue;
				}
				else{
					continue;
				}
			}
		}

	//	value_set *write_target=sst_is_insert(is, target_pair, &target_sst_file);
		if(sst_is_insert(is, target_pair)){
			if(write_function){
				write_function(is, des);
			}
		}
		
		sst_os_pop(os_set[target_idx]);
		if(sst_os_is_empty(os_set[target_idx])){
			one_empty=true;
			all_empty_check|=1<<target_idx;
		}
	}
}

static void write_sst_file(sst_in_stream *is, level *des){
	sst_file *sptr;
	value_set *vs=sst_is_get_result(is, &sptr);
	sptr->ppa=page_manager_get_new_ppa(LSM.pm,true);
	
	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=MAPPINGW;
	write_req->param=(void*)vs;
	write_req->end_req=comp_alreq_end_req;

	io_manager_issue_internal_write(sptr->ppa, vs, write_req, false);
	level_append_sstfile(des, sptr);
}


static sst_file *key_ptr_to_sst_file(key_ptr_pair *kp_set, bool should_flush){
	uint32_t ppa=UINT32_MAX;
	if(should_flush){
		value_set *vs=inf_get_valueset((char*)kp_set, FS_MALLOC_W, PAGESIZE);
		ppa=page_manager_get_new_ppa(LSM.pm, true);
		algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
		write_req->type=MAPPINGW;
		write_req->param=(void*)vs;
		write_req->end_req=comp_alreq_end_req;
		io_manager_issue_internal_write(ppa, vs, write_req, false);
	}
	sst_file *sstfile=sst_init(ppa, kp_set[0].lba, kp_set[LAST_KP_IDX].lba);
	return sstfile;
}

static void trivial_move(key_ptr_pair *kp_set,level *up, level *down, level *des){
	uint32_t ridx, sidx;
	run *rptr; sst_file *sptr; 
	if(kp_set){ // L0
		sst_file *file=key_ptr_to_sst_file(kp_set, true);
		if(down->now_sst_num==0){
			level_append_sstfile(des,file);
		}
		else{
			if(LAST_RUN_PTR(down)->end_lba < file->start_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr);
				}
			}
			level_append_sstfile(des,file);
			if(FIRST_RUN_PTR(down)->start_lba > file->end_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr);
				}
			}
		}
	}
	else{ //L1~LN-1
		if(down->now_sst_num==0){
			for_each_sst_level(up, rptr, ridx, sptr, sidx){
				level_append_sstfile(des, sptr);
			}
		}
		else{
			if(LAST_RUN_PTR(down)->end_lba < FIRST_RUN_PTR(up)->start_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr);
				}
			}

			for_each_sst_level(up, rptr, ridx, sptr, sidx){
				level_append_sstfile(des, sptr);
			}

			if(FIRST_RUN_PTR(down)->start_lba > LAST_RUN_PTR(up)->end_lba){
				for_each_sst_level(down, rptr, ridx, sptr, sidx){
					level_append_sstfile(des, sptr);
				}
			}

		}
	}
	free(kp_set);
}

level* compaction_first_leveling(compaction_master *cm, key_ptr_pair *kp_set, level *des){
	level *res=level_init(des->max_sst_num, des->run_num, des->istier, des->idx);
	if(!level_check_overlap_keyrange(kp_set[0].lba, kp_set[LAST_KP_IDX].lba, des)){
		trivial_move(kp_set,NULL,  des, res);
		return res;
	}
	/*each round read/write 128 data*/
	/*we need to have rate limiter!!*/
	read_issue_arg read_arg;
	read_arg.des=des;
	read_arg_container thread_arg;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg));
	thread_arg.arg_set[0]=&read_arg;
	thread_arg.set_num=1;

	sst_out_stream *os_set[2];
	sst_out_stream *os;
	sst_in_stream *is=sst_is_init();
	uint32_t round=des->now_sst_num/COMPACTION_TAGS+(des->now_sst_num/COMPACTION_TAGS?1:0);
	inter_read_alreq_param *read_param=cm->read_param;
	for(uint32_t i=0; i<round; i++){
		/*set read_arg*/
		read_arg.from=i*COMPACTION_TAGS;
		if(i!=round-1){
			read_arg.to=(i+1)*COMPACTION_TAGS-1;
		}
		else{
			read_arg.to=des->now_sst_num;
		}
		read_param_init(read_param, &read_arg);

		if(i==0){
			os=sst_os_init(LEVELING_SST_AT_PTR(des, read_arg.from), read_param, 
					read_arg.to-read_arg.from+1, read_done_check);
			os_set[0]=sst_os_init_kp(kp_set);
			os_set[1]=os;
		}
		else{
			sst_os_add(&os[1], LEVELING_SST_AT_PTR(des, read_arg.from), read_param,
					read_arg.to-read_arg.from+1);
		}
		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, 2, os_set, is, write_sst_file, i==round-1);

		read_param_free(read_param, &read_arg);
	}

	free(kp_set);
	free(thread_arg.arg_set);
	return res;
}

level* compaction_leveling(compaction_master *cm, level *src, level *des){
	level *res=level_init(des->max_sst_num, des->run_num, des->istier, des->idx);
	if(!level_check_overlap(src, des)){
		trivial_move(NULL,src, des, res);
		return res;
	}
	read_issue_arg read_arg1, read_arg2;
	read_arg1.des=src;
	read_arg2.des=des;

	read_arg_container thread_arg;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*2);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=2;

	sst_out_stream *os_set[2];
	sst_in_stream *is=sst_is_init();
	uint32_t total_sst_num=des->now_sst_num+src->now_sst_num;
	uint32_t round=total_sst_num/COMPACTION_TAGS+(total_sst_num/COMPACTION_TAGS?1:0);
	uint32_t ratio=des->now_sst_num/src->now_sst_num;
	ratio=ratio>COMPACTION_TAGS?1:ratio;

	uint32_t read_file_per_round_src=COMPACTION_TAGS/(ratio+1);
	uint32_t read_file_per_round_des=COMPACTION_TAGS/(ratio+1)*ratio;

	inter_read_alreq_param *read_param=cm->read_param;
	for(uint32_t i=0; i<round; i++){
		/*set read_arg*/
		if(i==0){
			read_arg1.from=0;
			read_arg1.to=(i+1)*read_file_per_round_src-1;

			read_arg2.from=0;
			read_arg2.to=(i+1)*read_file_per_round_des-1;
		}
		else if(i==round-1){
			read_arg1.from=i*read_file_per_round_src;
			read_arg1.to=(i+1)*read_file_per_round_src-1;

			read_arg2.from=i*read_file_per_round_des;
			read_arg2.to=(i+1)*read_file_per_round_des-1;
		}
		else{
			read_arg1.from=i*read_file_per_round_src;
			read_arg1.to=src->now_sst_num;

			read_arg2.from=i*read_file_per_round_des;
			read_arg2.to=des->now_sst_num;
		}

		read_param_init(&read_param[0], &read_arg1);
		uint32_t bound=read_arg1.to-read_arg2.from+1;
		read_param_init(&read_param[bound], &read_arg2);

		if(i==0){
			os_set[0]=sst_os_init(LEVELING_SST_AT_PTR(src, read_arg1.from), &read_param[0], 
					read_arg1.to-read_arg1.from+1, read_done_check);
			os_set[1]=sst_os_init(LEVELING_SST_AT_PTR(des, read_arg2.from), &read_param[bound], 
					read_arg2.to-read_arg2.from+1, read_done_check);
		}
		else{
			sst_os_add(os_set[0], LEVELING_SST_AT_PTR(src, read_arg1.from), &read_param[0],
					read_arg1.to-read_arg1.from+1);
			sst_os_add(os_set[1], LEVELING_SST_AT_PTR(des, read_arg2.from), &read_param[bound],
					read_arg2.to-read_arg2.from+1);
		}
		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, 2, os_set, is, write_sst_file, i==round-1);

		read_param_free(read_param, &read_arg1);
		read_param_free(read_param, &read_arg2);
	}
	
	free(thread_arg.arg_set);
	return res;

}

level* compaction_tiering(compaction_master *cm, level *src, level *des){ /*move to last level*/

	EPRINT("Not implemented", true);
	/*
	level *res=level_init(des->max_sst_num, des->run_num);
	read_issue_arg read_arg;
	read_arg.des=des;
	read_arg_container thread_arg;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg));
	thread_arg.arg_set[0]=&read_arg;
	thread_arg.set_num=1;

	set_out_stream *os;
	uint32_t round=src->now_sst_num/COMPACTION_TAGS + (des->now_sst_num/COMPACTION_TAGS?1:0);
	inter_read_alreq_param *read_param=cm->read_parasm;
	for(uint32_t i=0; i<round; i++){
	
	}*/
	return NULL;
}

level* compaction_merge(compaction_master *cm, run *r1, run* r2, uint8_t version_idx){
	EPRINT("Not implemented", true);
	return NULL;
}

void *comp_alreq_end_req(algo_req *req){
	inter_read_alreq_param *r_param;
	value_set *vs;
	switch(req->type){
		case MAPPINGW:
			vs=(value_set*)req->param;
			inf_free_valueset(vs, FS_MALLOC_W);
			break;
		case MAPPINGR:
			r_param=(inter_read_alreq_param*)req->param;
			fdriver_unlock(&r_param->done_lock);
			break;
	}
	free(req);
	return NULL;
}
