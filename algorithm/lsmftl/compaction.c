#include "comapction.h"
#include "run.h"
#include "../../include/sem_lock.h"
#include "../../interface/interface.h"

typedef struct{
	level *des;
	uint32_t from;
	uint32_t to;
}read_issue_arg;

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

uint32_t compaction_first_leveling(compaction_master *cm, key_ptr_pair *kp_set, level *des){
	/*each round read/write 128 data*/
	/*we need to have rate limiter!!*/
	read_issue_arg read_arg;
	read_arg.des=des;
	
	uint32_t kp_idx=0;
	key_ptr_pair kp_high, kp_low;
	

	sst_out_stream *os;
	sst_in_stread *is=sst_is_init();
	uint32_t round=des->now_sst_num/COMPACTION_TAGS+(des->now_sst_num/COMPACTION_TAGS?1:0);
	comp_read_alreq_params *read_params=cm->read_params;
	bool last=false, high_empty=false;
	for(uint32_t i=0; i<round; i++){
		if(i==round-1) last=true;
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
		}
		else{
			sst_os_add(os, LEVELING_SST_AT_PTR(des, read_arg.from), read_params,
					read_arg.to-read_arg.from+1);
		}
		/*send read I/O*/
		thpool_add_work(cm->issue_worker, read_sst_job, (void*)&read_arg);

		/*sorting data*/
		while(!high_emtpy && !sst_os_is_empty(os)){
			if(kp_idx!=PAGESIZE/sizeof(key_ptr_point)){
				kp_high=kp_set[kp_idx];
			}else{
				kp_high.lba=UINT32_MAX;
			}
			kp_low=sst_os_pick(os);

			if(is->vs==NULL){
				value_set *write_value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
				memset(write_value->value, -1, PAGESIZE);
				sst_is_set_space(is, inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE));

			}

			if(kp_low < kp_high){

			}else if(kp_low > kp_high){
			
			}
			else{

				invalidate_ppa;
			}
		}
		

		read_params_free(read_params, &read_arg);
		/*send write I/O*/
	}
}

uint32_t compaction_leveling(compaction_master *cm, level *src, level *des){

}

uint32_t compaction_tiering(compaction_master *cm, level *src, level *des){

}

uint32_t compaction_merge(compaction_master *cm, run *r1, run* r2, uint8_t version_idx){

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
