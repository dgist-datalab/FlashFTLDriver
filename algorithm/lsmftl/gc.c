#include "gc.h"
#include "lsmtree.h"
#include <queue>
#include <stdlib.h>
#include <stdio.h>

extern lsmtree LSM;
void __gc_mapping(page_manager *pm, blockmanager *bm, __gsegment *victim);
void __gc_data(page_manager *pm, blockmanager *bm, __gsegment *victim);

void *gc_end_req(algo_req* req);

static inline gc_read_node *gc_read_node_init(bool ismapping, uint32_t ppa){
	gc_read_node *res=(gc_read_node*)malloc(sizeof(gc_read_node));
	res->is_mapping=ismapping;
	res->data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	res->piece_ppa=ppa;
	res->lba=UINT32_MAX;
	fdriver_lock_init(&res->done_lock, 0);
	return res;
}

static inline void gc_read_node_free(gc_read_node *gn){
	//inf_free_valueset(gn->data, FS_MALLOC_R);
	fdriver_destroy(&gn->done_lock);
	free(gn);
}

static void gc_issue_read_node(gc_read_node *gn, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)gn;
	req->end_req=gc_end_req;
	if(gn->is_mapping){
		req->type=GCMR;
		li->read(gn->piece_ppa, PAGESIZE, gn->data, ASYNC, req);
	}
	else{
		req->type=GCDR;
		li->read(PIECETOPPA(gn->piece_ppa), PAGESIZE, gn->data, ASYNC, req);
	}
}

static void gc_issue_write_node(uint32_t ppa, value_set *data, bool ismap, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)data;
	req->type=ismap?GCMW:GCDW;
	li->read(ppa, PAGESIZE, data, ASYNC, req);
}

void *gc_end_req(algo_req *req){
	gc_read_node *gn;
	value_set *v;
	switch(req->type){
		case GCMR:
		case GCDR:
			gn=(gc_read_node*)req->param;
			fdriver_unlock(&gn->done_lock);
			break;
		case GCMW:
			v=(value_set*)req->param;
			inf_free_valueset(v, FS_MALLOC_R);
			break;
		case GCDW:

			break;
	}
	free(req);
	return NULL;
}


void do_gc(page_manager *pm){
	__gsegment *victim_target=pm->bm->get_gc_target(pm->bm);
	if(pm->seg_type_checker[victim_target->seg_idx]==DATASEG){
		__gc_data(pm, pm->bm, victim_target);
	}
	else{
		__gc_mapping(pm, pm->bm, victim_target);
	}
}

void __gc_mapping(page_manager *pm, blockmanager *bm, __gsegment *victim){
	if(victim->invalidate_number==_PPS){
		bm->trim_segment(bm, victim, bm->li);
		page_manager_change_reserve(pm, true);
		return;
	}
	else if(victim->invalidate_number>_PPS){
		EPRINT("????", true);
	}

	std::queue<gc_read_node*> *gc_target_queue=new std::queue<gc_read_node*>();
	uint32_t bidx;
	uint32_t pidx, page;
	gc_read_node *gn;

	for_each_page_in_seg(victim, page, bidx, pidx){
		if(bm->is_invalid_page(bm, page*L2PGAP)) continue;
		else{
			gn=gc_read_node_init(true, page);
			gc_target_queue->push(gn);
			gc_issue_read_node(gn, bm->li);
		}
	}

	while(!gc_target_queue->empty()){
		gn=gc_target_queue->front();
		fdriver_lock(&gn->done_lock);
		char *oob=bm->get_oob(bm, gn->piece_ppa);
		gn->lba=*(uint32_t*)oob;
		sst_file *target_sst_file=lsmtree_find_target_sst_mapgc(gn->lba, gn->piece_ppa);
		uint32_t ppa=page_manager_get_reserve_new_ppa(pm, true);
		target_sst_file->file_addr.map_ppa=ppa;
		gc_issue_write_node(ppa, gn->data, true, bm->li);
		gn->data=NULL;
		gc_read_node_free(gn);
		gc_target_queue->pop();
	}
	
	bm->trim_segment(bm, victim, bm->li);
	page_manager_change_reserve(pm, true);
}

void __gc_data(page_manager *pm, blockmanager *bm, __gsegment *victim){
	if(victim->invalidate_number==_PPS){
		bm->trim_segment(bm, victim, bm->li);
		page_manager_change_reserve(pm, false);
		return;
	}
	else if(victim->invalidate_number>_PPS){
		EPRINT("????", true);
	}

	std::queue<gc_read_node*> *gc_target_queue=new std::queue<gc_read_node*>();
	uint32_t bidx;
	uint32_t pidx, page;
	gc_read_node *gn;
	bool should_read;
	uint32_t read_page_num=0;
	
	for_each_page_in_seg(victim, page, bidx, pidx){
		should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm, page*L2PGAP+i)) continue;
			else{
				should_read=true;
				break;
			}
		}
		if(should_read){
			read_page_num++;
			gn=gc_read_node_init(false, page);
			gc_target_queue->push(gn);
			gc_issue_read_node(gn, bm->li);	
		}
	}

	write_buffer *gc_wb=write_buffer_init(_PPS*L2PGAP, pm,GC_WB);
	value_set **free_target=(value_set**)malloc(sizeof(value_set*)*read_page_num);

	uint32_t* oob_lba;
	uint32_t ppa;
	uint32_t q_idx=0;
	while(!gc_target_queue->empty()){
		gn=gc_target_queue->front();
		fdriver_lock(&gn->done_lock);
		ppa=PIECETOPPA(gn->piece_ppa);
		oob_lba=(uint32_t*)bm->get_oob(bm, gn->piece_ppa);

		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm, ppa*L2PGAP+i)) continue;
			else{
				if(oob_lba[i]==UINT32_MAX) continue;
				write_buffer_insert_for_gc(gc_wb, oob_lba[i], &gn->data->value[LPAGESIZE*i]);
			}
		}
		free_target[q_idx++]=gn->data;
	}

	key_ptr_pair *kp_set;
	while((kp_set=write_buffer_flush_for_gc(gc_wb, false))){
		LSM.moved_kp_set->push(kp_set);
	}

	for(uint32_t i=0; i<q_idx; i++){
		inf_free_valueset(free_target[i], FS_MALLOC_R);
	}
	bm->trim_segment(bm, victim, bm->li);
	page_manager_change_reserve(pm, false);
}
