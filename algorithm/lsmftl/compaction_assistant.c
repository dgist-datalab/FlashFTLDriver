#include "compaction.h"
#include "lsmtree.h"
#include "segment_level_manager.h"
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

static inline void disk_change(level **up, level *src, level** des, uint32_t* idx_set){
	if(up!=NULL){
		level *new_up=level_init((*up)->max_sst_num, (*up)->max_run_num, (*up)->istier, (*up)->idx);
		rwlock_write_lock(&LSM.level_rwlock[(*up)->idx]);
		level_free(*up, LSM.pm);
		*up=new_up;
		rwlock_write_unlock(&LSM.level_rwlock[(*up)->idx]);
	}

	level *delete_target_level=*des;
	uint32_t des_idx=(*des)->idx;
	//fdriver_lock(&LSM.level_lock[(*des)->idx]);
	rwlock_write_lock(&LSM.level_rwlock[des_idx]);
	(*des)=src;
	rwlock_write_unlock(&LSM.level_rwlock[des_idx]);
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
			key_ptr_pair *kp_set;

			if(req->wb){
				if(page_manager_get_total_remain_page(LSM.pm, false) < (KP_IN_PAGE/L2PGAP)){
					__do_gc(LSM.pm, false, KP_IN_PAGE/L2PGAP);
				}

				rwlock_write_lock(&LSM.flushed_kp_set_lock);
				rwlock_write_lock(&LSM.flush_wait_wb_lock);
				kp_set=write_buffer_flush(req->wb, false);
				write_buffer_free(req->wb);
				LSM.flushed_kp_set[req->tag]=kp_set;
				LSM.flush_wait_wb=NULL;
				//printf("kp_set set\n");
				rwlock_write_unlock(&LSM.flush_wait_wb_lock);
				rwlock_write_unlock(&LSM.flushed_kp_set_lock);
			}
			else{
				kp_set=req->target;
				if(kp_find_piece_ppa(807091, (char*)kp_set)!=UINT32_MAX){
					EPRINT("break\n", false);
				}
				if(kp_find_piece_ppa(464699, (char*)kp_set)!=UINT32_MAX){
					EPRINT("break\n", false);
				}
			}

			req->target=kp_set;

		//	uint32_t last_piece_ppa=kp_set[kp_end_idx((char*)kp_set)].piece_ppa;
		//	uint32_t first_piece_ppa=kp_set[0].piece_ppa;

			src=compaction_first_leveling(cm, kp_set, LSM.disk[0]);
			/*
			//printf("compaction done\n");
			if(SEGNUM(first_piece_ppa)==SEGNUM(last_piece_ppa)){
				slm_coupling_level_seg(0, SEGNUM(first_piece_ppa), SEGPIECEOFFSET(last_piece_ppa), req->gc_data);
			}
			else{
				slm_coupling_level_seg(0, SEGNUM(first_piece_ppa), _PPS*L2PGAP-1, req->gc_data);
				slm_coupling_level_seg(0, SEGNUM(last_piece_ppa), SEGPIECEOFFSET(last_piece_ppa), req->gc_data);
			}*/
			uint32_t end_idx=kp_end_idx((char*)kp_set);
			uint32_t prev_seg=UINT32_MAX;
			uint32_t prev_piece_ppa;

			if(SEGNUM(kp_set[0].piece_ppa)==SEGNUM(kp_set[end_idx].piece_ppa)){
				slm_coupling_level_seg(0, SEGNUM(kp_set[end_idx].piece_ppa), SEGPIECEOFFSET(kp_set[end_idx].piece_ppa), req->gc_data);
			}
			else{
				for(uint32_t i=0; i<=end_idx; i++){
					if(prev_seg==UINT32_MAX){
						prev_seg=SEGNUM(kp_set[i].piece_ppa);
						prev_piece_ppa=kp_set[i].piece_ppa;
						continue;
					}
					uint32_t now_seg=SEGNUM(kp_set[i].piece_ppa);
					if(now_seg==prev_seg){
						prev_piece_ppa=kp_set[i].piece_ppa;
					}
					else{
						slm_coupling_level_seg(0, prev_seg, SEGPIECEOFFSET(prev_piece_ppa), req->gc_data);
						prev_seg=now_seg;
						prev_piece_ppa=kp_set[i].piece_ppa;
					}
				}

				if(!slm_invalidate_enable(0, kp_set[end_idx].piece_ppa)){
					slm_coupling_level_seg(0, prev_seg, SEGPIECEOFFSET(prev_piece_ppa), req->gc_data);	
				}
			}

		}
		else if(req->end_level==LSM.param.LEVELN-1){
			src=compaction_tiering(cm, LSM.disk[req->start_level], LSM.disk[req->end_level]);
			slm_empty_level(req->start_level);
		}
		else{
			src=compaction_leveling(cm, LSM.disk[req->start_level], LSM.disk[req->end_level]);
			slm_move(req->end_level, req->start_level);
		}

		if(req->start_level==-1){
			disk_change(NULL, src, &LSM.disk[req->end_level], NULL);

			rwlock_write_lock(&LSM.flushed_kp_set_lock);
			LSM.flushed_kp_set[req->tag]=NULL;
			free(req->target);
			rwlock_write_unlock(&LSM.flushed_kp_set_lock);
		}
		else{
			disk_change(&LSM.disk[req->start_level], src, &LSM.disk[req->end_level], NULL);
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
			uint32_t merged_idx_set[2];
			src=compaction_merge(cm, LSM.disk[req->end_level], merged_idx_set);
			disk_change(NULL, src, &LSM.disk[req->end_level], merged_idx_set);
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
void compaction_wait(compaction_master *cm){
	while(cm->tm->tagQ->size()!=COMPACTION_REQ_MAX_NUM){}
}
