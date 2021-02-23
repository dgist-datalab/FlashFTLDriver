#include "compaction.h"
#include "lsmtree.h"
extern lsmtree LSM;

void* compaction_main(void *);
static volatile bool compaction_stop_flag;
compaction_master* compaction_init(uint32_t compaction_queue_num){
	compaction_master *cm=(compaction_master*)malloc(sizeof(compaction_master));
	cm->tm=tag_manager_init(compaction_queue_num);
	q_init(&cm->req_q, compaction_queue_num);

	cm->issue_worker=thpool_init(1);
	pthread_create(&cm->tid, NULL, compaction_main, (void*)cm);
	compaction_stop_flag=false;
	cm->read_param=(inter_read_alreq_param*)calloc(COMPACTION_TAGS, sizeof(inter_read_alreq_param));
	return cm;
}

void compaction_free(compaction_master *cm){
	compaction_stop_flag=true;
	pthread_join(cm->tid, NULL);
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

static inline void disk_change(level **up, level *src, level** des){
	level *new_up=level_init((*up)->max_sst_num, (*up)->max_run_num, (*up)->istier, (*up)->idx);
	fdriver_lock(&LSM.level_lock[(*up)->idx]);
	level_free(*up);
	*up=new_up;
	fdriver_unlock(&LSM.level_lock[(*up)->idx]);

	level *delete_target_level=*des;
	fdriver_lock(&LSM.level_lock[(*up)->idx]);
	(*des)=src;
	fdriver_unlock(&LSM.level_lock[(*up)->idx]);
	level_free(delete_target_level);
}

void* compaction_main(void *_cm){
	compaction_master *cm=(compaction_master*)_cm;
	queue *req_q=(queue*)cm->req_q;
	while(!compaction_stop_flag){
		compaction_req *req=(compaction_req*)q_dequeue(req_q);
		if(!req) continue;
again:
		level *src;
		if(req->start_level==-1 && req->end_level==0){
			src=compaction_first_leveling(cm, req->target, LSM.disk[0]);
		}
		else if(req->end_level==LSM.param.LEVELN-1){
			src=compaction_tiering(cm, LSM.disk[req->start_level], LSM.disk[req->end_level]);
		//	version_enqueue(LSM.last_run_version, LSM.version_num++);
		}
		else{
			src=compaction_leveling(cm, LSM.disk[req->start_level], LSM.disk[req->end_level]);
		}

		disk_change(&LSM.disk[req->start_level], src, &LSM.disk[req->end_level]);

		if(req->end_level != LSM.param.LEVELN-1 && level_is_full(LSM.disk[req->end_level])){
			req->start_level=req->end_level;
			req->end_level++;
			goto again;
		}
		else if(req->end_level==LSM.param.LEVELN-1 && level_is_full(LSM.disk[req->end_level])){
			EPRINT("not implemented", true);
			uint8_t start_run=version_get_oldest_run_idx(LSM.last_run_version);
		/*	version_dequeue(LSM.last_run_version);
			version_dequeue(LSM.last_run_version);

			compaction_merge(cm, LSM.disk[req->end_level]->array[start_run],
					LSM.disk[req->end_level]->array[start_run+1], 
					0);

		version_enqueue(LSM.last_run_version, LSM.version_num++);*/
		}
		req->end_req(req);
		tag_manager_free_tag(cm->tm,req->tag);
	}
	return NULL;
}
