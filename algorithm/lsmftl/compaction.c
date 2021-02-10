#include "comapction.h"
#include "run.h"
#include "../../include/sem_lock.h"
#include "../../interface/interface.h"

typedef struct{
	level *des;
	uint32_t from;
	uint32_t to;
}read_issue_arg;

typedef struct{
	read_issue_arg **arg_set;
	uint32_t set_num;
}read_arg_container;

void *comp_alreq_end_req(algo_req *req);
static void comp_read_alreq_init(algo_req *req){
	
}

static void comp_read_alreq_init(algo_req *req){

}

static void read_sst_job(void *arg, int th_num){

}

static void write_sst_job(/*??*/){

}

static void read_params_init(comp_read_qlreq_params *params, read_issue_arg *read_arg){
	for(int i=0; i<read_arg.from-read_arg.to+1; i++){
		params[i].target=LEVELING_SST_AT_PTR(read_ar.des, read_arg.from+i);
		params[i].data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
		fdriver_lock_init(&params[i].done_lock, 0);
	}
}


static void read_params_free(comp_read_qlreq_params *params, read_issue_arg *read_arg){
	for(int i=0; i<read_arg.from-read_arg.to+1; i++){
		//params[i].target=LEVELING_SST_AT_PTR(read_ar.des, read_arg.from+i);
		fdriver_lock_destroy(&params[i].done_lock);
	}
}

bool read_done_check(void *_params){
	comp_read_alreq_params *params=(comp_read_alreq_params*)_params;
	fdriver_lock(&params->doen_lock);
	params->target->data=params->data->value;
	return true;
}

static void stream_sorting(level *des, uint32_t stream_num, sst_out_stream **os_set, 
		sst_is_stream *is, void (*write_function)(value_set *, sst_file*,  level*), bool all_empty_stop){
	uint32_t target_idx=UINT32_MAX;
	bool one_empty=false;
	if(stream_num >= 32){
		EPRINT("too many stream!!", true);
	}
	uint32_t all_empty_check=0;
	sst_file *target_sst_file;
	while(!(all_empty_stop && all_empty_check==(1<<(stream_num)-1)) || !(!all_empty_stop && one_empty)){
		key_ptr_pair target_pair;
		for(uint32_t i=0; i<stream_num; i++){
			key_ptr_pair now=sst_os_pick(os_set[i]);
			if(target_idx==UINMT32_MAX){
				 target_pair=now;
				 target_idx=i;
			}
			else{
				if(target_pair.lba > now.lba){
					target_pair=now;
					target_idx=i;
				}
				else if(target_pair.lba==now.lba){
					invalidate_ppa_piece(now.ppa);
					sst_os_pop(os_set[i]);
					continue;
				}
				else{
					continue;
				}
			}
		}

		value_set *write_target=sst_is_insert(is, target_pair, &target_sst_file);
		if(write_target){
			if(write_function)
				write_function(write_target, target_sst_file, des);
		}
		
		sst_os_pop(os_set[target_idx]);
		if(sst_os_is_empty(os_set[target_idx])){
			one_empty=true;
			all_empty_check|=1<<target_idx;
		}
	}
}

void write_sst_file(value_set *value, sst_file *file, level *des){

}

level* compaction_first_leveling(compaction_master *cm, key_ptr_pair *kp_set, level *des){
	level *res=level_init(des->max_sst_num, des->run_num);
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
	sst_in_stread *is=sst_is_init();
	uint32_t round=des->now_sst_num/COMPACTION_TAGS+(des->now_sst_num/COMPACTION_TAGS?1:0);
	comp_read_alreq_params *read_params=cm->read_params;
	for(uint32_t i=0; i<round; i++){
		/*set read_arg*/
		read_arg.from=i*COMPACTION_TAG;
		if(i!=round-1){
			read_arg.to=(i+1)*COMPACTION_TAG-1;
		}
		else{
			read_arg.to=des->now_sst_num;
		}
		read_params_init(read_params, &read_arg);

		if(i=0){
			os=sst_os_init(LEVELING_SST_AT_PTR(des, read_arg.from), read_params, 
					read_arg.to-read_arg.from+1, read_done_check);
			os_set[0]=sst_os_init_kp(kp_set);
			os_set[1]=os;
		}
		else{
			sst_os_add(os[1], LEVELING_SST_AT_PTR(des, read_arg.from), read_params,
					read_arg.to-read_arg.from+1);
		}
		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&thread_arg);

		stream_sorting(res, 2, os_set, is, write_sst_file, i==round-1);

		read_params_free(read_params, &read_arg);
	}

	free(thread_arg.arg_set);
	return res;
}

level* compaction_leveling(compaction_master *cm, level *src, level *des){
	level *res=level_init(des->max_sst_num, des->run_num);
	read_issue_arg read_arg1, read_arg2;
	read_arg1.des=src;
	read_arg2.des=des;

	read_arg_container thread_arg;
	thread_arg.arg_set=(read_issue_arg**)malloc(sizeof(read_issue_arg*)*2);
	thread_arg.arg_set[0]=&read_arg1;
	thread_arg.arg_set[1]=&read_arg2;
	thread_arg.set_num=2;

	sst_out_stream *os_set[2];
	sst_in_stread *is=sst_is_init();
	uint32_t total_sst_num=des->now_sst_num+src->now_sst_num;
	uint32_t round=total_sst_num/COMPACTION_TAGS+(total_sst_num/COMPACTION_TAGS?1:0);
	uint32_t ratio=des->now_sst_num/src->now_sst_num;
	ratio=ratio>COMPACTION_TAG?1:ratio;

	uint32_t read_file_per_round_src=COMPACIONT_TAG/(ratio+1);
	uint32_t read_file_per_round_des=COMPACIONT_TAG/(ratio+1)*ratio;
	uint32_t cummulated_read_src=0;
	uint32_t cummulated_read_des=0;

	comp_read_alreq_params *read_params=cm->read_params;
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

		read_params_init(&read_params[0], &read_arg1);
		uint32_t bound=read_arg1.to-read_arg2.from+1;
		read_params_init(&read_params[bound], &read_arg2);

		if(i=0){
			os_set[0]=sst_os_init(LEVELING_SST_AT_PTR(src, read_arg1.from), &read_params[0], 
					read_arg1.to-read_arg1.from+1, read_done_check);
			os_set[1]=sst_os_init(LEVELING_SST_AT_PTR(des, read_arg2.from), &read_params[bound], 
					read_arg2.to-read_arg2.from+1, read_done_check);
		}
		else{
			sst_os_add(os[0], LEVELING_SST_AT_PTR(src, read_arg1.from), &read_params[0],
					read_arg1.to-read_arg1.from+1);
			sst_os_add(os[1], LEVELING_SST_AT_PTR(des, read_arg2.from), &read_params[bound],
					read_arg2.to-read_arg2.from+1);
		}
		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&arg_set);

		stream_sorting(res, 2, os_set, is, write_sst_file, i==round-1);

		read_params_free(read_params, &read_arg);
	}
	
	free(thread_arg.arg_set);
	return res;

}

level* compaction_tiering(compaction_master *cm, level *src, level *des){

}

level* compaction_merge(compaction_master *cm, run *r1, run* r2, uint8_t version_idx){

}

void *comp_alreq_end_req(algo_req *req){
	comp_alreq_params *params=(comp_alreq_params*)req->params;
	switch(req->params){
		case MAPPINGW:
			break;
		case MAPPINGR:
			break;
	}
}
