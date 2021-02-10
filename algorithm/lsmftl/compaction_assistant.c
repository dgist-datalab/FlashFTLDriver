#include "comapction.h"
#include "lsmtree.h"
extern lsmtree LSM;

void* compaction_main(void *);
static volatile bool compaction_stop_flag;
compaction_master* compaction_init(uint32_t compaction_queue_num){
	compaction_master *cm=(compaction_master*)malloc(sizeof(compaction_master));
	cm->tm=tag_manager_init(compaction_queue_num);
	q_init(&cm->req_q, compaction_queue_num);

	cm->comp_algo_req=slab_master_init(sizeof(algo_req), NUM_TAGS*2);
	cm->issue_worker=thpool_init(1);
	pthread_create(&cm->tid, NULL, compaction_main, (void*)cm);
	compaction_stop_flag=false;
	cm->read_params=(comp_read_alreq_params*)calloc(COMPACTION_TAGS, sizeof(comp_read_alreq_params));
	return cm;
}

void compaction_free(compcation_master *cm){
	compaction_stop_flag=true;
	pthread_join(&cm->tid, NULL);
	tag_manager_free_manager(cm->tm);
	q_free(cm->req_q);
	free(cm);
}

void compaction_issue_req(compaction_master *cm, compaction_req *req){
	if(compaction_stop_flag) return;
	req->tag=tag_manager_get_tag(cm->tm);
	q_enqueue((void*)req, cm->req_q);
	return;
}

void* compaction_main(void *_cm){
	compaction_master *cm=(compaction_master*)_cm;
	queue *req_q=(queue*)cm->req_q;
	while(!compaction_stop_flag){
		compaction_req *req=(compaction_req*)q_dequeue(req_q);
		if(!req) continue;
again:
		if(req->start_level==-1 && req->end_level==0){
			compaction_first_leveling(req->target, LSM.lev[0]);
		}
		else if(req->end_level==LSM.param.LEVELN-1){
			compaction_tiering(&LSM.lev[req->start_level], &LSM.lev[req->end_level]);
			version_enqueue(LSM.last_run_version);
		}
		else{
			compaction_leveling(&LSM.lev[req->start_level], &LSM.lev[req->end_level]);
		}

		if(req->end_level != LSM.param.LEVELN-1 && level_is_full(&LSM.lev[req->end_level])){
			req->start_level=req->end_level;
			req->end_level++;
			goto again;
		}
		else if(req->end_level==LSM.param.LEVELN-1 && level_is_full(&LSM.lev[req->end_level])){
			uint8_t start_run=version_get_oldest_run_idx(LSM.last_run_version);
			version_dequeue(LSM.last_run_version);
			version_dequeue(LSM.last_run_version);

			compaction_merge(&LSM.lev[req->end_level].array[start_run],
					&LSM.lev[req->end_level].array[start_run+1], 
					version_get_run_nxt(LSM.last_run_version));

			version_enqueue(LSM.last_run_version);
		}
		req->end_req((void*)req);
		tag_manager_free_tag(cm->tm,req->tag);
	}
	return NULL;
}
