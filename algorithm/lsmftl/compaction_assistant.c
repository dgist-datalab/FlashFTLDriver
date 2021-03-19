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

	cm->read_param_queue=new std::queue<inter_read_alreq_param*>();
	for(uint32_t i=0; i<COMPACTION_TAGS; i++){
		cm->read_param_queue->push(&cm->read_param[i]);
	}
	//cm->kv_wrapper_slab=slab_master_init(sizeof(key_value_wrapper), COMPACTION_TAGS);
	return cm;
}

void compaction_free(compaction_master *cm){
	compaction_stop_flag=true;
	pthread_join(cm->tid, NULL);
	tag_manager_free_manager(cm->tm);

	free(cm->read_param);
	delete cm->read_param_queue;

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
	if(up!=NULL){
		level *new_up=level_init((*up)->max_sst_num, (*up)->max_run_num, (*up)->istier, (*up)->idx);
		rwlock_write_lock(&LSM.level_rwlock[(*up)->idx]);
		level_free(*up, LSM.pm);
		*up=new_up;
		rwlock_write_unlock(&LSM.level_rwlock[(*up)->idx]);
	}

	level *delete_target_level=*des;
	//fdriver_lock(&LSM.level_lock[(*des)->idx]);
	rwlock_write_lock(&LSM.level_rwlock[(*des)->idx]);
	(*des)=src;
	rwlock_write_unlock(&LSM.level_rwlock[(*des)->idx]);
	//fdriver_unlock(&LSM.level_lock[(*des)->idx]);
	level_free(delete_target_level, LSM.pm);
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

		if(req->start_level==-1){
			disk_change(NULL, src, &LSM.disk[req->end_level]);
		}
		else{
			disk_change(&LSM.disk[req->start_level], src, &LSM.disk[req->end_level]);
		}

		if(req->end_level != LSM.param.LEVELN-1 && (
				(req->end_level==0 && level_is_full(LSM.disk[req->end_level])) ||
				(req->end_level!=0 && !level_is_appendable(LSM.disk[req->end_level], LSM.disk[req->end_level-1]->max_sst_num)))
				){
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
		tag_manager_free_tag(cm->tm,req->tag);
		req->end_req(req);
	}
	return NULL;
}

uint32_t compaction_read_param_remain_num(compaction_master *cm){
	return cm->read_param_queue->size();
}

inter_read_alreq_param *compaction_get_read_param(compaction_master *cm){
	if(cm->read_param_queue->size()==0){
		EPRINT("plz check size before get param", true);
	}
	inter_read_alreq_param *res=cm->read_param_queue->front();
	cm->read_param_queue->pop();
	return res;
}

void compaction_free_read_param(compaction_master *cm, inter_read_alreq_param *target){
	cm->read_param_queue->push(target);
	return;
}
