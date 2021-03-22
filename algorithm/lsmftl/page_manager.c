#include "page_manager.h"
#include "lsmtree.h"
#include <stdlib.h>
#include <stdio.h>

extern lsmtree LSM;

void validate_piece_ppa(blockmanager *bm, uint32_t piece_num, uint32_t *piece_ppa,
		uint32_t *lba){
	for(uint32_t i=0; i<piece_num; i++){
		if(piece_ppa[i]==4489547){
			printf("break\n");
		}
		if(piece_ppa[i]==2244608){
			printf("%u mapped to %u\n",lba[i], piece_ppa[i]);
		}
		char *oob=bm->get_oob(bm, PIECETOPPA(piece_ppa[i]));
		memcpy(&oob[(piece_ppa[i]%2)*sizeof(uint32_t)], &lba[i], sizeof(uint32_t));
		bm->populate_bit(bm, piece_ppa[i]);
	}
}


bool page_manager_oob_lba_checker(page_manager *pm, uint32_t piece_ppa, uint32_t lba, uint32_t *idx){
	char *oob=pm->bm->get_oob(pm->bm, PIECETOPPA(piece_ppa));
	for(uint32_t i=0; i<L2PGAP; i++){
		if((*(uint32_t*)&oob[i*sizeof(uint32_t)])==lba){
			*idx=i;
			return true;
		}
	}
	*idx=L2PGAP;
	return false;
}

void invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa){
	if(piece_ppa==4489547){
		printf("break\n");
	}

	bm->unpopulate_bit(bm, piece_ppa);
}

page_manager* page_manager_init(struct blockmanager *_bm){
	page_manager *pm=(page_manager*)calloc(1,sizeof(page_manager));
	pm->bm=_bm;
	pm->current_segment[DATA_S]=_bm->get_segment(_bm, false);
	pm->seg_type_checker[pm->current_segment[DATA_S]->seg_idx]=SEPDATASEG;
	pm->current_segment[MAP_S]=_bm->get_segment(_bm, false);
	pm->seg_type_checker[pm->current_segment[MAP_S]->seg_idx]=MAPSEG;
	pm->reserve_segment=_bm->get_segment(_bm, true);
	return pm;
}

void page_manager_free(page_manager* pm){
	free(pm->current_segment[DATA_S]);
	free(pm->current_segment[MAP_S]);
	free(pm->reserve_segment);
	free(pm);
}

void validate_map_ppa(blockmanager *bm, uint32_t map_ppa, uint32_t lba){
	char *oob=bm->get_oob(bm, map_ppa);
	*(uint32_t*)oob=lba;
	bm->populate_bit(bm, map_ppa*L2PGAP);
}

void invalidate_map_ppa(blockmanager *bm, uint32_t map_ppa){
	bm->unpopulate_bit(bm, map_ppa*L2PGAP);
}

uint32_t page_manager_get_new_ppa(page_manager *pm, bool is_map, uint32_t type){
	uint32_t res;
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];

	if(bm->check_full(bm, seg, MASTER_PAGE)){
		if(bm->is_gc_needed(bm)){
			//EPRINT("before get ppa, try to gc!!\n", true);
			__do_gc(pm);
		}
		free(seg);
		pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
		if(pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx==0){
			printf("break!\n");
		}
		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}
	res=bm->get_page_num(bm, seg);
	return res;
}

uint32_t page_manager_pick_new_ppa(page_manager *pm, bool is_map, uint32_t type){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(bm->check_full(bm, seg, MASTER_PAGE)){
		if(bm->is_gc_needed(bm)){
		//	EPRINT("before get ppa, try to gc!!\n", true);
			__do_gc(pm);
		}
		free(seg);

		pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
		if(pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx==0){
			printf("break!\n");
		}
		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}
	return bm->pick_page_num(bm, seg);
}

bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page, 
		bool is_map){
	blockmanager *bm=pm->bm;
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(seg->used_page_num + needed_page>= _PPS) return true;
	return bm->check_full(bm, seg, MASTER_PAGE) && bm->is_gc_needed(bm); 
}


uint32_t page_manager_get_remain_page(page_manager *pm, bool ismap){
	if(ismap){
		return _PPS-pm->current_segment[MAP_S]->used_page_num;
	}
	else{
		return _PPS-pm->current_segment[DATA_S]->used_page_num;
	}
}

uint32_t page_manager_get_reserve_new_ppa(page_manager *pm, bool ismap){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=ismap?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(bm->check_full(bm, seg, MASTER_PAGE)){
		free(seg);
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment;
		pm->reserve_segment=NULL;
		//pm->current_segment[ismap?MAP_S:DATA_S]=bm->get_segment(bm,false);
		//pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=is_map?MAPSEG:DATASEG;
		goto retry;
	}
	return bm->pick_page_num(bm, seg);
}

uint32_t page_manager_change_reserve(page_manager *pm, bool ismap){
	blockmanager *bm=pm->bm;
	if(pm->reserve_segment==NULL){
		pm->reserve_segment=bm->change_reserve(bm, pm->current_segment[ismap?MAP_S:DATA_S]);
	}
	return 1;
}

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


void __do_gc(page_manager *pm){
	__gsegment *victim_target=pm->bm->get_gc_target(pm->bm);
	switch(pm->seg_type_checker[victim_target->seg_idx]==DATASEG){
		case DATASEG:
		case SEPDATASEG:
			__gc_data(pm, pm->bm, victim_target);
			break;
		case MAPSEG:
			__gc_mapping(pm, pm->bm, victim_target);
			break;

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
	if(victim->invalidate_number==_PPS*2){
		bm->trim_segment(bm, victim, bm->li);
		page_manager_change_reserve(pm, false);
		return;
	}
	else if(victim->invalidate_number>_PPS*2){
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
	sst_file *sptr;
	uint32_t recent_version, target_version;
	while(!gc_target_queue->empty()){
		gn=gc_target_queue->front();
		fdriver_lock(&gn->done_lock);
		ppa=PIECETOPPA(gn->piece_ppa);
		oob_lba=(uint32_t*)bm->get_oob(bm, ppa);

		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm, ppa*L2PGAP+i)) continue;
			else{
				if(oob_lba[i]==UINT32_MAX) continue;
				/*check invalidation*/
				uint32_t recent_version=version_map_lba(LSM.last_run_version, oob_lba[i]);
				sptr=level_find_target_run_idx(LSM.disk[LSM.param.LEVELN-1], oob_lba[i], ppa*L2PGAP+i, &target_version);
				if(sptr==NULL){
					/*checking other level*/

				}
				else{
					if(recent_version==target_version){
						sptr->already_invalidate_file=true;
						write_buffer_insert_for_gc(gc_wb, oob_lba[i], &gn->data->value[LPAGESIZE*i]);
					}
					else continue; //already invalidate
				}

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
