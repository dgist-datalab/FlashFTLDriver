#include "compaction.h"
#include "lsmtree.h"
#include "segment_level_manager.h"
#include <math.h>
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
		level *new_up=level_init((*up)->max_sst_num, (*up)->max_run_num, (*up)->level_type, (*up)->idx);
		level_free(*up, LSM.pm);
		*up=new_up;
	}

	level *delete_target_level=*des;
	(*des)=src;
	level_free(delete_target_level, LSM.pm);
}

static inline void first_level_slm_coupling(key_ptr_pair *kp_set, compaction_req *req){
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

static inline key_ptr_pair* flush_memtable(write_buffer *wb, uint32_t tag){
	key_ptr_pair *kp_set;
	if(page_manager_get_total_remain_page(LSM.pm, false) < (KP_IN_PAGE/L2PGAP)){
		__do_gc(LSM.pm, false, KP_IN_PAGE/L2PGAP);
	}

	rwlock_write_lock(&LSM.flush_wait_wb_lock);
	rwlock_write_lock(&LSM.flushed_kp_set_lock);
	kp_set=write_buffer_flush(wb, false);
	write_buffer_free(wb);
	LSM.flushed_kp_set[tag]=kp_set;
	LSM.flush_wait_wb=NULL;
	//printf("kp_set set\n");
	rwlock_write_unlock(&LSM.flush_wait_wb_lock);
	rwlock_write_unlock(&LSM.flushed_kp_set_lock);
	return kp_set;
}

static inline void do_compaction(compaction_master *cm, compaction_req *req, 
		key_ptr_pair *kp_set, uint32_t start_idx, uint32_t end_idx){
	level *lev;
	if(kp_set){
		rwlock_write_lock(&LSM.level_rwlock[end_idx]);
	}else{
		rwlock_write_lock(&LSM.level_rwlock[end_idx]);
		rwlock_write_lock(&LSM.level_rwlock[start_idx]);
	}
	
	if(kp_set){
		lev=compaction_first_leveling(cm, kp_set, LSM.disk[0]);
	}
	else{
		uint32_t end_type=LSM.disk[end_idx]->level_type;
		switch(LSM.disk[start_idx]->level_type){
			case LEVELING:
				if(end_type==LEVELING){
					//compaction_LE2LE(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
				}
				else{
					//compaction_LE2TI(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
				}
				break;
			case LEVELING_WISCKEY:
				switch(end_type){
					case LEVELING_WISCKEY:
						compaction_LW2LW(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
						break;
					case LEVELING:
						compaction_LW2LE(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
						break;
					case TIERING:
						compaction_LW2TI(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
						break;
				}
				break;
			case TIERING:
				compaction_TI2TI(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
				break;
		}
	}
	disk_change(kp_set?NULL:&LSM.disk[start_idx], lev, &LSM.disk[end_idx], NULL);

	if(end_idx==LSM.param.LEVELN-1){
		slm_empty_level(start_idx);
	}
	else if(end_idx==0){
		first_level_slm_coupling(kp_set, req);
	}
	else{
		slm_move(start_idx, end_idx);
	}

	if(kp_set){
		rwlock_write_unlock(&LSM.level_rwlock[end_idx]);
		rwlock_write_lock(&LSM.flushed_kp_set_lock);
		LSM.flushed_kp_set[req->tag]=NULL;
		free(kp_set);
		rwlock_write_unlock(&LSM.flushed_kp_set_lock);
	}
	else{
		rwlock_write_unlock(&LSM.level_rwlock[end_idx]);
		rwlock_write_unlock(&LSM.level_rwlock[start_idx]);
	}
}

void* compaction_main(void *_cm){
	compaction_master *cm=(compaction_master*)_cm;
	queue *req_q=(queue*)cm->req_q;
	while(!compaction_stop_flag){
		compaction_req *req=(compaction_req*)q_dequeue(req_q);
		if(!req) continue;
again:
		level *src;
		key_ptr_pair *kp_set=NULL;
		if(req->start_level==-1 && req->end_level==0){
			if(req->wb){
				kp_set=flush_memtable(req->wb, req->tag);
			}
			else{
				kp_set=req->target;
			}
			req->target=kp_set;
		}

		do_compaction(cm, req, kp_set, req->start_level, req->end_level);

		if(req->end_level != LSM.param.LEVELN-1 && (
				(req->end_level==0 && level_is_full(LSM.disk[req->end_level])) ||
				(req->end_level!=0 && !level_is_appendable(LSM.disk[req->end_level], LSM.disk[req->end_level-1]->max_sst_num)))
				){
			req->start_level=req->end_level;
			req->end_level++;
			goto again;
		}
		else if(req->end_level==LSM.param.LEVELN-1 && level_is_full(LSM.disk[req->end_level])){
			uint32_t merged_idx_set[MERGED_RUN_NUM];
			rwlock_write_lock(&LSM.level_rwlock[req->end_level]);
			src=compaction_merge(cm, LSM.disk[req->end_level], merged_idx_set);
			disk_change(NULL, src, &LSM.disk[req->end_level], merged_idx_set);
			rwlock_write_unlock(&LSM.level_rwlock[req->end_level]);
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
	if(cm->read_param_queue->size()>COMPACTION_TAGS){
		EPRINT("debug point", false);
	}
	cm->read_param_queue->push(target);
	return;
}
void compaction_wait(compaction_master *cm){
	while(cm->tm->tagQ->size()!=COMPACTION_REQ_MAX_NUM){}
}
