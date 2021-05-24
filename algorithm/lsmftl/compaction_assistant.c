#include "compaction.h"
#include "lsmtree.h"
#include "segment_level_manager.h"
#include "io.h"
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

static inline void first_level_slm_coupling(key_ptr_pair *kp_set, bool is_gc_data){
	uint32_t end_idx=kp_end_idx((char*)kp_set);
	uint32_t prev_seg=UINT32_MAX;
	uint32_t prev_piece_ppa;

	if(SEGNUM(kp_set[0].piece_ppa)==SEGNUM(kp_set[end_idx].piece_ppa)){
		slm_coupling_level_seg(0, SEGNUM(kp_set[end_idx].piece_ppa), SEGPIECEOFFSET(kp_set[end_idx].piece_ppa), is_gc_data);
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
				slm_coupling_level_seg(0, prev_seg, SEGPIECEOFFSET(prev_piece_ppa), is_gc_data);
				prev_seg=now_seg;
				prev_piece_ppa=kp_set[i].piece_ppa;
			}
		}

		if(!slm_invalidate_enable(0, kp_set[end_idx].piece_ppa)){
			slm_coupling_level_seg(0, prev_seg, SEGPIECEOFFSET(prev_piece_ppa), is_gc_data);	
		}
	}
}
extern char all_set_page[PAGESIZE];
static sst_file *kp_to_sstfile(std::map<uint32_t, uint32_t> *flushed_kp_set, 
		std::map<uint32_t, uint32_t>::iterator *temp_iter){
	if(temp_iter && *temp_iter==flushed_kp_set->end()){
		return NULL;
	}
	value_set *vs=inf_get_valueset(all_set_page, FS_MALLOC_W, PAGESIZE);
	key_ptr_pair *kp_set=(key_ptr_pair*)vs->value;

	std::map<uint32_t, uint32_t>::iterator iter=temp_iter?*temp_iter:flushed_kp_set->begin();
	uint32_t i=0;
	read_helper *rh=read_helper_init(
			lsmtree_get_target_rhp(0));
	for(; iter!=flushed_kp_set->end() && i<KP_IN_PAGE; i++, iter++ ){
		kp_set[i].lba=iter->first;
		kp_set[i].piece_ppa=iter->second;
		read_helper_stream_insert(rh, kp_set[i].lba, kp_set[i].piece_ppa);
	}
	*temp_iter=iter;

	uint32_t map_ppa=page_manager_get_new_ppa(LSM.pm, false, DATASEG); //DATASEG for sequential tiering 
	validate_map_ppa(LSM.pm->bm, map_ppa, kp_set[0].lba,  kp_set[i].lba, true);
	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=MAPPINGW;
	write_req->param=(void*)vs;
	write_req->end_req=comp_alreq_end_req;
	io_manager_issue_internal_write(map_ppa, vs, write_req, false);

	sst_file *res=sst_init_empty(PAGE_FILE);
	res->file_addr.map_ppa=map_ppa;

	res->start_lba=kp_set[0].lba;
	res->end_lba=kp_set[i].lba;
	res->_read_helper=rh;

	if(SEGNUM(kp_set[0].piece_ppa)==SEGNUM(kp_set[i].piece_ppa) && 
			SEGNUM(kp_set[0].piece_ppa)==map_ppa/_PPS){
		res->sequential_file=true;
	}
	return res;
}

static inline level* flush_memtable(write_buffer *wb, bool is_gc_data){
	if(page_manager_get_total_remain_page(LSM.pm, false) < wb->buffered_entry_num/L2PGAP){
		__do_gc(LSM.pm, false, KP_IN_PAGE/L2PGAP);
	}
	
	rwlock_write_lock(&LSM.flush_wait_wb_lock);
	rwlock_write_lock(&LSM.flushed_kp_set_lock);
	if(LSM.flushed_kp_set==NULL){
		LSM.flushed_kp_set=new std::map<uint32_t, uint32_t>();
	}
	while(wb->buffered_entry_num){
		key_ptr_pair *temp_kp_set=write_buffer_flush(wb, false);
		for(uint32_t i=0; i<KP_IN_PAGE && temp_kp_set[i].lba!=UINT32_MAX; i++){
			LSM.flushed_kp_set->insert(
					std::pair<uint32_t, uint32_t>(temp_kp_set[i].lba, temp_kp_set[i].piece_ppa));	
		}
		free(temp_kp_set);
	}
	write_buffer_free(wb);
	LSM.flush_wait_wb=NULL;
	rwlock_write_unlock(&LSM.flushed_kp_set_lock);
	rwlock_write_unlock(&LSM.flush_wait_wb_lock);

	if(LSM.flushed_kp_set->size()>=LSM.param.write_buffer_ent){
		level *res=level_init(LSM.param.write_buffer_ent/KP_IN_PAGE, 1, LEVELING_WISCKEY, 0);
		sst_file *sptr=NULL;
		std::map<uint32_t, uint32_t>::iterator *iter=NULL;
		while((sptr=kp_to_sstfile(LSM.flushed_kp_set, iter))){
			level_append_sstfile(res, sptr, true);
		}
		return res;
	}
	else{
		return NULL;
	}
}

static inline void do_compaction(compaction_master *cm, compaction_req *req, 
		level *temp_level, uint32_t start_idx, uint32_t end_idx){
	level *lev=NULL;
	if(temp_level){
		rwlock_write_lock(&LSM.level_rwlock[end_idx]);
	}else{
		rwlock_write_lock(&LSM.level_rwlock[end_idx]);
		rwlock_write_lock(&LSM.level_rwlock[start_idx]);
	}

	uint32_t end_type=LSM.disk[end_idx]->level_type;
	switch(start_idx!=-1?LSM.disk[start_idx]->level_type:LSM.disk[0]->level_type){
		case LEVELING:
			if(end_type==LEVELING){
				lev=compaction_LE2LE(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
			}
			else{
				lev=compaction_LE2TI(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
			}
			break;
		case LEVELING_WISCKEY:
			switch(end_type){
				case LEVELING_WISCKEY:
					lev=compaction_LW2LW(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
					break;
				case LEVELING:
					lev=compaction_LW2LE(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
					break;
				case TIERING:
					lev=compaction_LW2TI(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
					break;
			}
			break;
		case TIERING:
			lev=compaction_TI2TI(cm, LSM.disk[start_idx], LSM.disk[end_idx]);
			break;
	}

	disk_change(temp_level?NULL:&LSM.disk[start_idx], lev, &LSM.disk[end_idx], NULL);

	if(end_idx==LSM.param.LEVELN-1){
		slm_empty_level(start_idx);
	}
	else if(end_idx==0){
	//	first_level_slm_coupling(kp_set, req);
	}
	else{
		slm_move(start_idx, end_idx);
	}

	if(temp_level){
		rwlock_write_unlock(&LSM.level_rwlock[end_idx]);
		rwlock_write_lock(&LSM.flushed_kp_set_lock);
		std::map<uint32_t, uint32_t> *temp_map=LSM.flushed_kp_set;
		delete temp_map;
		LSM.flushed_kp_set=NULL;
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
		level *temp_level=NULL;
		if(req->start_level==-1 && req->end_level==0){
			if(req->wb){
				temp_level=flush_memtable(req->wb, false);
			}
			else{
				EPRINT("wtf", true);
			}
		}

		if(req->start_level==-1 && !temp_level){
			goto end;
		}

		do_compaction(cm, req, temp_level, req->start_level, req->end_level);

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
end:
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
