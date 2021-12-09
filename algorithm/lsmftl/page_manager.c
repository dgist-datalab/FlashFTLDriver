#include "page_manager.h"
#include "oob_manager.h"
#include "io.h"
#include "lsmtree.h"
#include "compaction.h"
#include <stdlib.h>
#include <stdio.h>
#include<queue>

extern lsmtree LSM;
//uint32_t debug_piece_ppa=1105510*L2PGAP;
//uint32_t debug_piece_ppa=386798*L2PGAP;
//uint32_t debug_piece_ppa=868351*L2PGAP;
uint32_t debug_piece_ppa=715125*L2PGAP;
bool temp_debug_flag;
extern uint32_t debug_lba;

static inline void testing_seg_num(__segment *s){
#ifdef LSM_DEBUG
	uint32_t total_num=0;
	for(uint32_t i=0; i<BPS;i++){
		total_num+=s->blocks[i]->now;
	}
	if(total_num!=s->used_page_num){
		printf("wtf!\n"); //already different
		abort();
	}
#endif
}

void validate_piece_ppa(blockmanager *bm, uint32_t piece_num, uint32_t *piece_ppa,
		uint32_t *lba, uint32_t *version, bool should_abort){
#ifdef LSM_DEBUG
	static int cnt=0;
#endif
	for(uint32_t i=0; i<piece_num; i++){
		oob_manager *oob=get_oob_manager(PIECETOPPA(piece_ppa[i]));
		oob->lba[piece_ppa[i]%L2PGAP]=lba[i];
		oob->version[piece_ppa[i]%L2PGAP]=version[i];
		oob->ismap=false;
	//	memcpy(&oob[(piece_ppa[i]%L2PGAP)*sizeof(uint32_t)], &lba[i], sizeof(uint32_t));
#ifdef LSM_DEBUG
		if(piece_ppa[i]==debug_piece_ppa){
			printf("%u lba:%u ppa:%u", should_abort?++cnt:cnt, oob->lba[piece_ppa[i]%L2PGAP], debug_piece_ppa);
			EPRINT("validate piece here!\n", false );
			if(cnt==13){
				printf("break!\n");
			}
		}
#endif

		if(!bm->bit_set(bm, piece_ppa[i]) && should_abort){
			EPRINT("bit error", true);
		}
	}
}

static inline void gc_debug_checking(gc_read_node *gn){
#ifdef LSM_DEBUG
	if(gn->piece_ppa/L2PGAP==425967 && LSM.global_debug_flag){
		EPRINT("debug point", false);
	}

	if(gn->piece_ppa==debug_piece_ppa && gn->lba==debug_lba){
		EPRINT("break!", false);
	}
	if(LSM.global_debug_flag && gn->piece_ppa==debug_piece_ppa){
		EPRINT("break!", false);
	}
#endif
}

bool page_manager_oob_lba_checker(page_manager *pm, uint32_t piece_ppa, uint32_t lba, uint32_t *idx){
	oob_manager *oob=get_oob_manager(PIECETOPPA(piece_ppa));
	if(get_lba_from_oob_manager(oob, piece_ppa%L2PGAP)==lba){
		*idx=piece_ppa%L2PGAP;
		return true;
	}

	*idx=L2PGAP;
	return false;

//	char *oob=pm->bm->get_oob(pm->bm, PIECETOPPA(piece_ppa));
//	if(*(uint32_t*)&oob[(piece_ppa%L2PGAP)*sizeof(uint32_t)]==lba){
//		*idx=piece_ppa%L2PGAP;
//		return true;
//	}
//	*idx=L2PGAP;
//	return false;
}

bool invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool should_abort){
#ifdef LSM_DEBUG
	if(piece_ppa==debug_piece_ppa){
		static uint32_t cnt=0;
		printf("%u %u", should_abort?++cnt:++cnt, debug_piece_ppa);
		EPRINT("invalidate piece here!\n",false);
		if(cnt==13){
			printf("break!\n");
		}
	}
#endif
	if(piece_ppa==UINT32_MAX){
		/*already gced piece*/
		return true;
	}

	/*first query at check*/
	if(!should_abort && !bm->bit_query(bm, piece_ppa)){
		return false;
	}
	if(!bm->bit_unset(bm, piece_ppa)){
		if(should_abort){
			EPRINT("bit error", true);
		}
	}
	return true;
}

page_manager* page_manager_init(struct blockmanager *_bm){
	page_manager *pm=(page_manager*)calloc(1,sizeof(page_manager));
	pm->bm=_bm;
	pm->current_segment[DATA_S]=_bm->get_segment(_bm, BLOCK_ACTIVE);
	pm->seg_type_checker[pm->current_segment[DATA_S]->seg_idx]=SEPDATASEG;
	pm->reserve_segment[DATA_S]=_bm->get_segment(_bm, BLOCK_RESERVE);

	pm->remain_data_segment_q=new std::list<__segment*>();

	return pm;
}

static void check_victim_is_active(page_manager *pm, __gsegment *victim){
	/*reserve block is not managed by heap*/
	uint32_t v_seg_idx=victim->seg_idx;
	if(pm->current_segment[MAP_S] &&
			pm->current_segment[MAP_S]->seg_idx==v_seg_idx){
		free(pm->current_segment[MAP_S]);
		pm->current_segment[MAP_S]=NULL;
	}
	
	if(pm->current_segment[DATA_S] && 
			pm->current_segment[DATA_S]->seg_idx==v_seg_idx){
		free(pm->current_segment[DATA_S]);
		pm->current_segment[DATA_S]=NULL;
	}

	std::list<__segment*>::iterator iter;
	for(iter=pm->remain_data_segment_q->begin(); 
			iter!=pm->remain_data_segment_q->end();){
		if((*iter)->seg_idx==v_seg_idx){
			free((*iter));
			pm->remain_data_segment_q->erase(iter);
			break;
		}
		iter++;
	}
}

void page_manager_free(page_manager* pm){
	delete pm->remain_data_segment_q;

	free(pm->current_segment[DATA_S]);
	free(pm->current_segment[MAP_S]);
	free(pm->reserve_segment[DATA_S]);
	free(pm->reserve_segment[MAP_S]);
	free(pm);
}

void validate_map_ppa(blockmanager *bm, uint32_t map_ppa, uint32_t start_lba, uint32_t end_lba, bool should_abort){
	oob_manager *oob=get_oob_manager(map_ppa);
	oob->lba[0]=start_lba;
	oob->lba[1]=end_lba;
	oob->ismap=true;

//	char *oob=bm->get_oob(bm, map_ppa);
//	((uint32_t*)oob)[0]=start_lba;
//	((uint32_t*)oob)[1]=end_lba;
#ifdef LSM_DEBUG
	if(map_ppa==debug_piece_ppa/L2PGAP){
		static int cnt=0;
		printf("%u %u", should_abort?++cnt:cnt, debug_piece_ppa);
		EPRINT("validate map here!\n", false);
		printf("[info] map_ppa:%u, s~e %u ~ %u\n", map_ppa, start_lba, end_lba);
	}
#endif

	for(uint32_t i=0; i<L2PGAP; i++){
		if(!bm->bit_set(bm, map_ppa*L2PGAP+i) && should_abort){
			EPRINT("bit error", true);
		}
	}
}

void invalidate_map_ppa(blockmanager *bm, uint32_t map_ppa, bool should_abort){
	bool flag=should_abort;
#if defined(DEMAND_SEG_LOCK) || defined(UPDATING_COMPACTION_DATA)
	if(LSM.blocked_invalidation_seg[map_ppa/_PPS]){
		flag=false;
	}
#endif

#ifdef LSM_DEBUG
	if(map_ppa==debug_piece_ppa/L2PGAP){
		static int cnt=0;
		printf("%u %u", should_abort?++cnt:cnt, debug_piece_ppa);
		EPRINT("invalidate map here!\n", false);
		//lsmtree_compactioning_set_print(map_ppa/_PPS);
	}
#endif

	for(uint32_t i=0; i<L2PGAP; i++){
		if(!should_abort && !bm->bit_query(bm, map_ppa *L2PGAP +i)){
			continue;
		}
		if(!bm->bit_unset(bm, map_ppa*L2PGAP+i)){
			if(flag){
				EPRINT("bit error", true);
			} 
		}
	}
}

uint32_t page_manager_get_new_ppa(page_manager *pm, bool is_map, uint32_t type){
	uint32_t res;
	blockmanager *bm=pm->bm;
	bool temp_used=false;
	__segment *seg;
retry:
	if(!LSM.function_test_flag && pm->remain_data_segment_q->size() && !is_map){
		temp_used=true;
		seg=pm->remain_data_segment_q->front();
	}
	else{
		seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	}
	if(!seg || bm->check_full(seg)){
		if(temp_used){
			temp_used=false;
			pm->remain_data_segment_q->pop_front();
			goto retry;
		}
		if(bm->is_gc_needed(bm)){
			//EPRINT("before get ppa, try to gc!!\n", true);
			if(__do_gc(pm,is_map, is_map?1:(1+1))){ //just trim
				if(!pm->current_segment[is_map?MAP_S:DATA_S]){
					pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
				}
				goto retry;
			}
			else{ //copy trim
				
			}
			if(pm->current_segment[is_map?MAP_S:DATA_S] &&
					pm->current_segment[is_map?MAP_S:DATA_S]->used_page_num<_PPS){
			
			}
			else{
				pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
			}
			
		}
		else{
			if(seg){
			}
			pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
		}

		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}
	res=bm->get_page_addr(seg);
	return res;
}

__segment *page_manager_get_seg(page_manager *pm, bool is_map, uint32_t type){
	blockmanager *bm=pm->bm;
	bool temp_used=false;
retry:
	__segment *seg=NULL;
	if(pm->remain_data_segment_q->size() && !is_map){
		temp_used=true;
		seg=pm->remain_data_segment_q->front();
		pm->remain_data_segment_q->pop_front();
	}
	else{
		seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	}

	if(!seg || bm->check_full(seg)){
		if(temp_used){
			temp_used=false;
	//		pm->remain_data_segment_q->pop_front();
			goto retry;
		}
		if(bm->is_gc_needed(bm)){
			//EPRINT("before get ppa, try to gc!!\n", true);
			if(__do_gc(pm,is_map, _PPS)){ //just trim
			}
			else{ //copy trim
				
			}
			if(!pm->current_segment[is_map?MAP_S:DATA_S]){
				pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
			}
		}
		else{
			if(seg){
			}
			pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
		}

		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}

	if(!temp_used){
		pm->current_segment[is_map?MAP_S:DATA_S]=NULL;
	}
	return seg;
}

__segment *page_manager_get_seg_for_bis(page_manager *pm,  uint32_t type){
retry:
	__segment *seg=page_manager_get_seg(pm, false, type);
	if(_PPS-seg->used_page_num<=1){
		goto retry;	
	}
	return seg;
}

uint32_t page_manager_get_new_ppa_from_seg(page_manager *pm, __segment *seg){
	uint32_t res;
	blockmanager *bm=pm->bm;
	if(bm->check_full(seg)){
		EPRINT("plz check gc before get new_ppa", true);
	}
	res=bm->get_page_addr(seg);
	return res;
}

uint32_t page_manager_pick_new_ppa(page_manager *pm, bool is_map, uint32_t type){
	blockmanager *bm=pm->bm;
	bool temp_used=false;
	__segment *seg;
retry:
	if(!is_map &&  pm->remain_data_segment_q->size()){
		temp_used=true;
		seg=pm->remain_data_segment_q->front();
	}
	else{
		seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	}

	if(!seg || bm->check_full(seg)){
		if(temp_used){
			temp_used=false;
			pm->remain_data_segment_q->pop_front();
			goto retry;
		}
		if(bm->is_gc_needed(bm)){
			//EPRINT("before get ppa, try to gc!!\n", true);
			if(__do_gc(pm,is_map, is_map?1:(1+1))){ //just trim
			}
			else{ //copy trim
				
			}
			if(!pm->current_segment[is_map?MAP_S:DATA_S]){
				pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
			}
		}
		else{
			if(seg){
			}
			pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,BLOCK_ACTIVE);
		}

		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=type;
		goto retry;
	}
	return bm->pick_page_addr(seg);
}

uint32_t page_manager_pick_new_ppa_from_seg(page_manager *pm, __segment *seg){
	uint32_t res;
	blockmanager *bm=pm->bm;
	if(bm->check_full(seg)){
		EPRINT("plz check gc before get new_ppa", true);
	}
	res=bm->pick_page_addr(seg);
	return res;
}

bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page, 
		bool is_map){
	blockmanager *bm=pm->bm;
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(seg->used_page_num + needed_page>= _PPS) return true;
	return bm->check_full(seg) && bm->is_gc_needed(bm); 
}


uint32_t page_manager_get_remain_page(page_manager *pm, bool ismap){
	if(ismap){
		return pm->current_segment[MAP_S]?_PPS-pm->current_segment[DATA_S]->used_page_num:0;
	}
	else{
		if(pm->remain_data_segment_q->size()){
			return _PPS-pm->remain_data_segment_q->front()->used_page_num;
		}
		return pm->current_segment[DATA_S]?_PPS-pm->current_segment[DATA_S]->used_page_num:0;
	}
}

uint32_t page_aligning_data_segment(page_manager *pm, uint32_t target_page_addr){
	//bool isfinish=false;
	__segment *seg;
	uint32_t q_size=pm->remain_data_segment_q->size();
	for(uint32_t i=0; i<q_size; i++){
		seg=pm->remain_data_segment_q->front();
		if(_PPS-seg->used_page_num <= target_page_addr){
			pm->remain_data_segment_q->pop_front();
		}
		else{
			return _PPS-seg->used_page_num;
		}
	}

	if(!pm->current_segment[DATA_S] ||
			_PPS-pm->current_segment[DATA_S]->used_page_num <= target_page_addr){
		page_manager_move_next_seg(LSM.pm, false, false, DATASEG);
	}

	return _PPS-pm->current_segment[DATA_S]->used_page_num;
}

uint32_t page_manager_get_total_remain_page(page_manager *pm, bool ismap, bool include_inv_block){
	if(ismap){
		return pm->bm->total_free_page_num(pm->bm, pm->current_segment[MAP_S]);
	}
	else{
		uint32_t inv_blk_num=include_inv_block?pm->bm->invalidate_seg_num(pm->bm):0;
		uint32_t temp_data_page_num=0;
		if(pm->remain_data_segment_q->size()){
			seg_list_iter iter=pm->remain_data_segment_q->begin();
			for(;iter!=pm->remain_data_segment_q->end(); iter++){
				temp_data_page_num+=_PPS-(*iter)->used_page_num;
			}
		}
		if(pm->current_segment[DATA_S]){
			return pm->bm->total_free_page_num(pm->bm, pm->current_segment[DATA_S])+temp_data_page_num+inv_blk_num*_PPS;
		}
		else{
			return pm->bm->total_free_page_num(pm->bm, NULL)+temp_data_page_num+inv_blk_num*_PPS;
		}
	}
}

uint32_t page_manager_get_reserve_new_ppa(page_manager *pm, bool ismap, uint32_t seg_idx){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=ismap?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];

	if(!seg || bm->check_full(seg) || seg->seg_idx==seg_idx){
		if(seg){
		}
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment[ismap?MAP_S:DATA_S];
		pm->reserve_segment[ismap?MAP_S:DATA_S]=NULL;
		pm->seg_type_checker[pm->current_segment[ismap?MAP_S:DATA_S]->seg_idx]=ismap?MAPSEG:DATASEG;
		goto retry;
	}
	return bm->get_page_addr(seg);
}

uint32_t page_manager_get_reserve_remain_ppa(page_manager *pm, bool ismap, uint32_t seg_idx){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=ismap?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	
	if(!seg || bm->check_full(seg) || seg->seg_idx==seg_idx){
		if(seg){
		}
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment[ismap?MAP_S:DATA_S];
		pm->reserve_segment[ismap?MAP_S:DATA_S]=NULL;
		pm->seg_type_checker[pm->current_segment[ismap?MAP_S:DATA_S]->seg_idx]=ismap?MAPSEG:DATASEG;
		goto retry;
	}
	return _PPS-seg->used_page_num;
}

uint32_t page_manager_change_reserve(page_manager *pm, bool ismap){
	blockmanager *bm=pm->bm;
	if(pm->reserve_segment[ismap?MAP_S:DATA_S]==NULL){
		bm->change_reserve_to_active(bm, pm->current_segment[ismap?MAP_S:DATA_S]);
		pm->reserve_segment[ismap?MAP_S:DATA_S]=bm->get_segment(bm, BLOCK_RESERVE);
	}
	return 1;
}

uint32_t page_manager_move_next_seg(page_manager *pm, bool ismap, bool isreserve, uint32_t type){
	if(isreserve){
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->reserve_segment[ismap?MAP_S:DATA_S];
		pm->reserve_segment[ismap?MAP_S:DATA_S]=NULL;
	}
	else{
		pm->current_segment[ismap?MAP_S:DATA_S]=pm->bm->get_segment(pm->bm, BLOCK_ACTIVE);
		pm->seg_type_checker[pm->current_segment[ismap?MAP_S:DATA_S]->seg_idx]=type;
	}
	return 1;
}

bool __gc_mapping(page_manager *pm, blockmanager *bm, __gsegment *victim);
bool __gc_data(page_manager *pm, blockmanager *bm, __gsegment *victim);

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
		li->read(gn->piece_ppa, PAGESIZE, gn->data,  req);
	}
	else{
		req->type=GCDR;
		li->read(PIECETOPPA(gn->piece_ppa), PAGESIZE, gn->data,  req);
	}
}

static void gc_issue_write_node(uint32_t ppa, value_set *data, bool ismap, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)data;
	req->type=ismap?GCMW:GCDW;
	req->end_req=gc_end_req;
	li->write(ppa, PAGESIZE, data,  req);
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
		case GCDW:
		case GCMW:
			v=(value_set*)req->param;
			inf_free_valueset(v, FS_MALLOC_R);
			break;
	}
	free(req);
	return NULL;
}


bool  __do_gc(page_manager *pm, bool ismap, uint32_t target_page_addr){
	bool res=false;
	__gsegment *victim_target;
	std::queue<uint32_t> temp_queue;
	std::queue<uint32_t> diff_type_queue;
	uint32_t seg_idx;
	uint32_t remain_page=0;
retry:
	victim_target=pm->bm->get_gc_target(pm->bm);
	if(victim_target==NULL){
		lsmtree_seg_debug(&LSM);
	}
	seg_idx=victim_target->seg_idx;

	if(ismap){
		if(pm->seg_type_checker[seg_idx]!=MAPSEG &&  
				(pm->current_segment[DATA_S]==NULL || pm->current_segment[DATA_S]->seg_idx!=seg_idx) && 
				victim_target->all_invalid){
			goto out;
		}
		else if(pm->seg_type_checker[seg_idx]==MAPSEG){
			goto out;
		}
	}
	else{
		if(pm->seg_type_checker[seg_idx]==MAPSEG &&  
				(pm->current_segment[MAP_S]==NULL || pm->current_segment[MAP_S]->seg_idx!=seg_idx) && 
				victim_target->all_invalid){
			goto out;
		}
		else if(pm->seg_type_checker[seg_idx]!=MAPSEG){
			goto out;
		}
	}

retry_logic:
	free(victim_target);
	if(ismap && pm->seg_type_checker[seg_idx]!=MAPSEG){
		diff_type_queue.push(seg_idx);
	}
	else if(!ismap && pm->seg_type_checker[seg_idx]==MAPSEG){
		diff_type_queue.push(seg_idx);
	}
	else{
		temp_queue.push(seg_idx);
	}
	goto retry;

out:
	//if(lsmtree_is_gc_available(&LSM, seg_idx) && victim_target->invalidate_number!=victim_target->validate_number){
	if(lsmtree_is_gc_unavailable(&LSM, seg_idx)){
//		if(LSM.global_debug_flag) printf("[%lu] %u is locked\n", temp_queue.size(), seg_idx);
		goto retry_logic;
	}
	else if(victim_target->invalidate_piece_num<=L2PGAP*2 && !victim_target->all_invalid){
//		if(LSM.global_debug_flag) printf("[%lu] %u is not enough invalid\n", temp_queue.size(), seg_idx);
		goto retry_logic;
	}


	if(LSM.flushed_kp_seg->find(seg_idx)!=LSM.flushed_kp_seg->end()){
		res=true;
	}

	switch(pm->seg_type_checker[seg_idx]){
		case DATASEG:
		case SEPDATASEG:
			while(diff_type_queue.size()){
				seg_idx=diff_type_queue.front();
				pm->bm->insert_gc_target(pm->bm, seg_idx);
				diff_type_queue.pop();
			}
			__gc_data(pm, pm->bm, victim_target);
			remain_page=page_manager_get_total_remain_page(LSM.pm, false, false);
			break;
		case MAPSEG:
			while(diff_type_queue.size()){
				seg_idx=diff_type_queue.front();
				pm->bm->insert_gc_target(pm->bm, seg_idx);
				diff_type_queue.pop();
			}
			__gc_mapping(pm, pm->bm, victim_target);
			remain_page=page_manager_get_total_remain_page(LSM.pm, true, false);
			break;
	}
	while(temp_queue.size()){
		seg_idx=temp_queue.front();
		pm->bm->insert_gc_target(pm->bm, seg_idx);
		temp_queue.pop();
	}
	if(remain_page<target_page_addr)
		goto retry;

	return res;
}

bool __gc_mapping(page_manager *pm, blockmanager *bm, __gsegment *victim){
	LSM.monitor.gc_mapping++;
//	printf("gc_mapping:%u (seg_idx:%u)\n", LSM.monitor.gc_mapping, victim->seg_idx);
	check_victim_is_active(pm, victim);
	if(victim->invalidate_piece_num==_PPS*L2PGAP || victim->all_invalid){
#ifdef LSM_DEBUG
		if(debug_piece_ppa/L2PGAP/_PPS==victim->seg_idx){
			printf("gc_mapping:%u (seg_idx%u) clean\n", LSM.monitor.gc_mapping, victim->seg_idx);
		}
#endif
		bm->trim_segment(bm, victim);
		page_manager_change_reserve(pm, true);
		return true;
	}
	else if(victim->invalidate_piece_num>_PPS*L2PGAP){
		EPRINT("????", true);
	}
	
	//static int cnt=0;
	//printf("mapping gc:%u\n", ++cnt);

	std::queue<gc_read_node*> *gc_target_queue=new std::queue<gc_read_node*>();
	uint32_t bidx;
	uint32_t pidx, page;
	gc_read_node *gn;

	for_each_page_in_seg(victim, page, bidx, pidx){
		if(bm->is_invalid_piece(bm, page*L2PGAP)) continue;
		else{
			gn=gc_read_node_init(true, page);
			gc_target_queue->push(gn);
			gc_issue_read_node(gn, bm->li);
		}
	}

	while(!gc_target_queue->empty()){
		fdriver_lock(&gn->done_lock);
		gn=gc_target_queue->front();

		oob_manager *oob=get_oob_manager(gn->piece_ppa);
		gn->lba=oob->lba[0];

//		char *oob=bm->get_oob(bm, gn->piece_ppa);
//		gn->lba=*(uint32_t*)oob;
		sst_file *target_sst_file=lsmtree_find_target_sst_mapgc(gn->lba, gn->piece_ppa);
		target_sst_file->sequential_file=false;
		invalidate_map_ppa(pm->bm, gn->piece_ppa, true);

		uint32_t ppa=page_manager_get_reserve_new_ppa(pm, true, victim->seg_idx);
		target_sst_file->file_addr.map_ppa=ppa;
		validate_map_ppa(pm->bm, ppa, gn->lba, target_sst_file->end_lba, true);
		gc_issue_write_node(ppa, gn->data, true, bm->li);
		gn->data=NULL;
		gc_read_node_free(gn);
		gc_target_queue->pop();
	}
	
	bm->trim_segment(bm, victim);
	page_manager_change_reserve(pm, true);

	delete gc_target_queue;
	return false;
}

typedef struct gc_sptr_node{
	sst_file *sptr;
	uint32_t lev_idx;
	uint32_t version;
	uint32_t sidx;
	uint32_t early_invalidation_cnt;
	write_buffer *wb;
}gc_sptr_node;

static gc_sptr_node * gc_sptr_node_init(sst_file *sptr, uint32_t validate_num, 
		uint32_t lev_idx, uint32_t version, uint32_t sidx){
	gc_sptr_node *res=(gc_sptr_node*)malloc(sizeof(gc_sptr_node));
	//res->gc_kv_node=new std::queue<key_value_pair>();
	res->sptr=sptr;
	res->wb=write_buffer_init_for_gc(_PPS*L2PGAP, LSM.pm, GC_WB,LSM.param.plr_rhp);
	res->lev_idx=lev_idx;
	res->version=version;
	res->sidx=sidx;
	res->early_invalidation_cnt=0;
	return res;
}

static inline void gc_sptr_node_free(gc_sptr_node *gsn){
	write_buffer_free(gsn->wb);
	free(gsn);
}

static void insert_target_sptr(gc_sptr_node* gsn, uint32_t lba, char *value){
	write_buffer_insert_for_gc(gsn->wb, lba, value);
}

bool updating_now_compactioning_data(uint32_t version, uint32_t seg_idx, uint32_t lba, uint32_t new_piece_ppa){
	bool res=false;
	if(lba==debug_lba && LSM.global_debug_flag){
		printf("updating break!\n");
	}

	if(LSM.compactioning_pos_set){
		sst_file *file;
		map_range *mr;
		key_ptr_pair *kp_set=NULL;
		for(uint32_t i=0; i<LSM.now_compaction_stream_num; i++){
			sst_pf_out_stream *pos=LSM.compactioning_pos_set[i];
			if(!pos) continue;
			if(pos->type==SST_PAGE_FILE_STREAM){			
				std::multimap<uint32_t, sst_file*>::iterator f_iter=
					pos->sst_map_for_gc->lower_bound(lba);

				if(f_iter!=pos->sst_map_for_gc->begin()){
					do{
						std::multimap<uint32_t, sst_file*>::iterator f_iter_temp=--f_iter;
						file=f_iter_temp->second;
						if(file->start_lba <=lba && file->end_lba >=lba){
							f_iter=f_iter_temp;
						}
						else{
							f_iter++;
							break;
						}
					}while(f_iter!=pos->sst_map_for_gc->begin());
				}

				for(;f_iter!=pos->sst_map_for_gc->end(); f_iter++){
					file=f_iter->second;
					if(file->end_lba < lba ) continue;
					if(file->start_lba > lba) break;
					if(!file->data && file->read_done) continue;
					while(!file->read_done){}
					kp_set=(key_ptr_pair*)file->data;
					uint32_t kp_idx=kp_find_idx(lba, (char*)kp_set);
					if(kp_idx==UINT32_MAX || kp_set[kp_idx].piece_ppa/L2PGAP/_PPS!=seg_idx) continue;
					kp_set[kp_idx].piece_ppa=new_piece_ppa;
					res=true;
				}
			}
			else{
				std::multimap<uint32_t, map_range*>::iterator m_iter;
				m_iter=pos->mr_map_for_gc->lower_bound(lba); 

				if(m_iter!=pos->mr_map_for_gc->begin()){
					do{
						std::multimap<uint32_t, map_range*>::iterator m_iter_temp=--m_iter;
						mr=m_iter_temp->second;
						if(mr->start_lba <= lba && mr->end_lba>=lba){
							m_iter=m_iter_temp;
						}
						else{
							m_iter++;
							break;
						}
					}while(m_iter!=pos->mr_map_for_gc->begin());
				}

				for(; m_iter!=pos->mr_map_for_gc->end(); m_iter++){
					mr=m_iter->second;
					if(mr->end_lba < lba) continue;
					if(mr->start_lba > lba) break;
					if(!mr->data && mr->read_done) continue;
					while(!mr->read_done){}
					kp_set=(key_ptr_pair*)mr->data;
					uint32_t kp_idx=kp_find_idx(lba, (char*)kp_set);
					if(kp_idx==UINT32_MAX || kp_set[kp_idx].piece_ppa/L2PGAP/_PPS!=seg_idx) continue;
					kp_set[kp_idx].piece_ppa=new_piece_ppa;
					res=true;
				}
			}
		}
	}

	/*updating mapping data*/
	if(LSM.read_arg_set){
		for(uint32_t i=0; i<LSM.now_compaction_stream_num; i++){
			read_issue_arg *temp=LSM.read_arg_set[i];
			if(temp->version_for_gc!=version) continue;
			key_ptr_pair *kp_set=NULL;
			for(uint32_t j=temp->from; j<=temp->to; j++){
				if(temp->page_file==false){
					map_range *mr=&temp->map_target_for_gc[j];
					if(!mr) continue; //mr can be null, when it used all data.
					if(mr->ppa/_PPS!=seg_idx) continue;
					if(mr->start_lba > lba ||
							mr->end_lba < lba) continue;
					if(!mr->data && mr->read_done) continue;
					while(!mr->read_done){}
					kp_set=(key_ptr_pair*)temp->map_target_for_gc[j].data;
				}
				else{
					if(temp->sst_target_for_gc[j]->start_lba > lba ||
							temp->sst_target_for_gc[j]->end_lba < lba) continue;
					kp_set=(key_ptr_pair*)temp->sst_target_for_gc[j]->data;
				}


				if(!kp_set){
					continue;
				}

				uint32_t kp_idx=kp_find_idx(lba, (char*)kp_set);
				if(kp_idx==UINT32_MAX || kp_set[kp_idx].piece_ppa/L2PGAP/_PPS!=seg_idx) continue;
#ifdef LSM_DEBUG
				if(lba==debug_lba){
					printf("%u ppa:%u", lba, new_piece_ppa);
					EPRINT("is hit in mapping\n", false);
				}
#endif
				kp_set[kp_idx].piece_ppa=new_piece_ppa;
			}
		}
	}

	if(new_piece_ppa==UINT32_MAX){ //invalidation
		return res;
	}

	/*updating bos data*/
	sst_bf_out_stream *bos=LSM.now_compaction_bos;
	if(bos){
		std::map<uint32_t, struct key_value_wrapper*>::iterator iter;
		iter=bos->map_for_gc->find(lba);
		if(iter!=bos->map_for_gc->end() &&
				iter->second->piece_ppa/L2PGAP/_PPS==seg_idx){
#ifdef LSM_DEBUG
			if(lba==debug_lba){
				printf("%u ppa:%u", lba, new_piece_ppa);
				EPRINT("is hit in bos\n", false);
			}
#endif
			iter->second->piece_ppa=new_piece_ppa;
			res=true;
		}
	}
	if(lba==debug_lba && LSM.global_debug_flag){
		printf("updating end res:%u! \n", res);
	}
	return res;
}

bool inserting_new_map_ppa_for_invalidation(uint32_t new_ppa, uint32_t version){
	if(LSM.read_arg_set){
		for(uint32_t i=0; i<LSM.now_compaction_stream_num; i++){
			read_issue_arg *temp=LSM.read_arg_set[i];
			if(temp->version_for_gc!=version) continue;
			LSM.gc_moved_map_ppa.push(new_ppa);
			return true;
		}
	}
	return false;
}

static void move_sptr(gc_sptr_node *gsn, uint32_t seg_idx, uint32_t lev_idx, 
		uint32_t version, uint32_t sidx){
	// if no kv in gsn sptr should be checked trimed;
	if(gsn->wb->buffered_entry_num==0){
		gsn->sptr->trimed_sst_file=true;
	}
	else{
		sst_file *sptr=NULL;
		uint32_t round=0;
		uint32_t ridx=version_to_ridx(LSM.last_run_version, lev_idx, version);
		uint32_t before_contents_num=read_helper_get_cnt(gsn->sptr->_read_helper);
		uint32_t after_contents_num=0;
		while(gsn->wb->buffered_entry_num){
			sptr=sst_init_empty(BLOCK_FILE);
			uint32_t map_num=gsn->wb->buffered_entry_num/KP_IN_PAGE+
			(gsn->wb->buffered_entry_num%KP_IN_PAGE?1:0);
			key_ptr_pair **kp_set=(key_ptr_pair**)malloc(sizeof(key_ptr_pair*)*map_num);
			uint32_t kp_set_idx=0;
			key_ptr_pair *now_kp_set;
			bool force_stop=false;
			while((now_kp_set=write_buffer_flush_for_gc(gsn->wb, false, seg_idx, &force_stop, 
							kp_set_idx, NULL))){
				kp_set[kp_set_idx]=now_kp_set;
				kp_set_idx++;
				if(force_stop) break;
			}

#ifdef UPDATING_COMPACTION_DATA
			for(uint32_t i=0; i<kp_set_idx; i++){
				now_kp_set=kp_set[i];
				for(uint32_t j=0; j<KP_IN_PAGE && now_kp_set[j].lba!=UINT32_MAX ;j++){
					updating_now_compactioning_data(version, seg_idx, now_kp_set[j].lba,
							now_kp_set[j].piece_ppa);
				}
			}
#endif

			uint32_t map_ppa;
			map_range *mr_set=(map_range*)malloc(sizeof(map_range) * kp_set_idx);
			for(uint32_t i=0; i<kp_set_idx; i++){
				algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
				value_set *data=inf_get_valueset((char*)kp_set[i], FS_MALLOC_W, PAGESIZE);
				write_req->type=GCDW;
				write_req->param=data;
				write_req->end_req=gc_end_req;
				map_ppa=page_manager_get_reserve_new_ppa(LSM.pm, false, seg_idx);

				mr_set[i].start_lba=kp_set[i][0].lba;
				mr_set[i].end_lba=kp_get_end_lba((char*)kp_set[i]);
				mr_set[i].ppa=map_ppa;
				validate_map_ppa(LSM.pm->bm, map_ppa, mr_set[i].start_lba,mr_set[i].end_lba, true);
#ifdef UPDATING_COMPACTION_DATA
				inserting_new_map_ppa_for_invalidation(map_ppa, version);
#endif
				io_manager_issue_write(map_ppa, data, write_req, false);
			}

			//	free(sptr->block_file_map);
			sptr->block_file_map=mr_set;

			sptr->file_addr.piece_ppa=kp_set[0][0].piece_ppa;
			sptr->end_ppa=map_ppa;
			if(sptr->file_addr.piece_ppa/L2PGAP/_PPS != sptr->end_ppa/_PPS){
				EPRINT("should same segment" ,true);
			}
			sptr->map_num=kp_set_idx;
			sptr->start_lba=kp_set[0][0].lba;
			sptr->end_lba=mr_set[kp_set_idx-1].end_lba;
			sptr->_read_helper=gsn->wb->rh;
			read_helper_insert_done(gsn->wb->rh);

			after_contents_num+=read_helper_get_cnt(sptr->_read_helper);
			if(round==0){
				level_sptr_update_in_gc(LSM.disk[lev_idx], ridx, sidx, sptr);
			}
			else{
				level_sptr_add_at_in_gc(LSM.disk[lev_idx], ridx, sidx+round, sptr);
			}

			for(uint32_t i=0; i<kp_set_idx; i++){
				free(kp_set[i]);
			}

			gsn->wb->rh=NULL;
			round++;
			free(sptr);
			free(kp_set);
		}
		//uint32_t after_contents_num=read_helper_get_cnt(sptr->_read_helper);
		level_contents_num_updates_at_gc(LSM.disk[lev_idx],
				ridx, before_contents_num-after_contents_num);
		version_decrease_invalidation_number(LSM.last_run_version, gsn->version, 
				gsn->early_invalidation_cnt);
	}
	gc_sptr_node_free(gsn);
}

gc_mapping_check_node* gmc_init(gc_read_node *gn, sst_file *sptr, uint32_t version){
	gc_mapping_check_node *gmc=(gc_mapping_check_node*)malloc(sizeof(gc_mapping_check_node));
	gmc->data_ptr=&gn->data->value[LPAGESIZE*(gn->piece_ppa%L2PGAP)];
	gmc->piece_ppa=gn->piece_ppa;
	gmc->new_piece_ppa=UINT32_MAX;
	gmc->lba=gn->lba;
	gmc->map_ppa=UINT32_MAX;
	gmc->type=MAP_CHECK_FLUSHED_KP;
	gmc->level=LSM.param.LEVELN-(1+1);
	gmc->mapping_data=NULL;
	gmc->sptr=sptr;
	gmc->version=version;
	gmc->is_issued_node=false;
	gmc->is_page_file_map=false;
	gmc->is_invalidate_node=false;
	return gmc;
}

bool __gc_data(page_manager *pm, blockmanager *bm, __gsegment *victim){
	LSM.monitor.gc_data++;
	lsmtree_block_already_gc_seg(&LSM, victim->seg_idx);
	check_victim_is_active(pm, victim);
	if(victim->all_invalid){
		fdriver_lock(&LSM.now_gc_seg_lock);
		LSM.now_gc_seg_idx=UINT32_MAX;
		fdriver_unlock(&LSM.now_gc_seg_lock);
		bm->trim_segment(bm, victim);
		page_manager_change_reserve(pm, false);

		return true;
	}
	else if(victim->invalidate_piece_num>_PPS*L2PGAP){
		EPRINT("????", true);
	}
	
	printf("gc_data:%u (seg_idx:%u)\n", LSM.monitor.gc_data, victim->seg_idx);
	if(LSM.monitor.gc_data==505){
		printf("break!\n");
	}
	/*
	if(LSM.monitor.gc_data==531){
		//LSM.global_debug_flag=true;
	}*/
	std::queue<gc_read_node*> *gc_target_queue=new std::queue<gc_read_node*>();
	uint32_t bidx;
	uint32_t pidx, page;
	gc_read_node *gn;
	bool should_read;
	uint32_t read_page_num=0;
	uint32_t valid_piece_ppa_num=0;
	uint32_t temp[_PPS*L2PGAP];

	for_each_page_in_seg(victim, page, bidx, pidx){
		should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_piece(bm, page*L2PGAP+i)) continue;
			else{
				should_read=true;
				temp[valid_piece_ppa_num++]=page*L2PGAP+i;
			}
		}
		if(should_read){
			read_page_num++;
			gn=gc_read_node_init(false, page*L2PGAP);
			gc_target_queue->push(gn);
			gc_issue_read_node(gn, bm->li);	
		}
	}
/*
	if(((_PPS*L2PGAP-victim->invalidate_piece_num)-(victim->validate_piece_num-victim->invalidate_piece_num))>=(1+1)){
		if(valid_piece_ppa_num+victim->invalidate_piece_num!=victim->validate_piece_num){
			printf("valid list\n");
			for(uint32_t i=0; i<BPS; i++){
				printf("%u invalid_number:%u validate_number:%u\n", i,victim->blocks[i]->invalidate_piece_num, victim->blocks[i]->validate_piece_num);
			}
			printf("_PPS:%u BPS:%u\n", _PPS, BPS);
			EPRINT("not match validation!",true);
		}
	}
*/
	value_set **free_target=(value_set**)malloc(sizeof(value_set*)*read_page_num);

	uint32_t* oob_lba;
	uint32_t* oob_version;
	uint32_t ppa, piece_ppa;
	uint32_t q_idx=0;
	sst_file *sptr=NULL;
	uint32_t recent_version, target_version;
	uint32_t sptr_idx=0, level_idx=UINT32_MAX;

	write_buffer *wisckey_node_wb=NULL, *page_file_wb=NULL;
	std::map<uint32_t, gc_mapping_check_node *> *gc_kv_map=new std::map<uint32_t, gc_mapping_check_node*>();
	std::map<uint32_t, gc_mapping_check_node*> *gc_page_file_kv_map=new std::map<uint32_t, gc_mapping_check_node*>();
	std::multimap<uint32_t, gc_mapping_check_node*> *gc_page_file_map=new std::multimap<uint32_t, gc_mapping_check_node*>();
	std::multimap<uint32_t, gc_mapping_check_node*> *gc_page_file_invalid_kv_map=new std::multimap<uint32_t, gc_mapping_check_node*>();

	gc_mapping_check_node *gmc=NULL;

	gc_sptr_node *gsn=NULL;
	while(!gc_target_queue->empty()){
		gn=gc_target_queue->front();
		fdriver_lock(&gn->done_lock);
		ppa=PIECETOPPA(gn->piece_ppa);
		oob_manager *oob=get_oob_manager(ppa);
		oob_lba=oob->lba;
		oob_version=oob->version;
		if(LSM.global_debug_flag && gn->piece_ppa==debug_piece_ppa){
			printf("break!\n");
		}
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_piece(bm, ppa*L2PGAP+i)){
				continue;
			}
			else{
				if(oob_lba[i]==UINT32_MAX) continue;
				if(ppa==debug_piece_ppa/L2PGAP){
					static int debug_point=0;
					printf("debug_point %u break!\n", debug_point++);
				}
				piece_ppa=ppa*L2PGAP+i;
				gn->piece_ppa=piece_ppa;
				gn->lba=oob_lba[i];
				gn->version=oob_version[i];
				if(gn->lba==debug_lba){
					static int debug_point=0;
					printf("debug_point:%u\n", debug_point++);
					printf("target is moved!\n");
				}
				
	//			gc_debug_checking(gn);

				/*check invalidation*/
				if(!sptr || 
						(sptr && (((!sptr->sequential_file && sptr->end_ppa*L2PGAP<piece_ppa ) || (sptr->sequential_file && sptr->seq_data_end_piece_ppa<piece_ppa))
								  && !is_map_ppa(sptr, piece_ppa/L2PGAP))))
				{
					if(sptr){
						if(gsn->wb->buffered_entry_num){
							move_sptr(gsn,victim->seg_idx, gsn->lev_idx, gsn->version, gsn->sidx);
						}
						else{
							uint32_t ridx=version_to_ridx(LSM.last_run_version, gsn->lev_idx, gsn->version);
							level_contents_num_updates_at_gc(LSM.disk[gsn->lev_idx], 
									ridx, 
									read_helper_get_cnt(gsn->sptr->_read_helper));
							version_decrease_invalidation_number(LSM.last_run_version, 
									gsn->version, gsn->early_invalidation_cnt);
							/*
							for(uint32_t i=0; i<sptr->map_num; i++){
								map_range *mr=&sptr->block_file_map[i];
								printf("%u~%u ppa:%u\n", mr->start_lba, mr->end_lba, mr->ppa);
							}
							printf("now compactioning print\n");
							*/
							level_sptr_remove_at_in_gc(LSM.disk[gsn->lev_idx], ridx, gsn->sidx);
							//lsmtree_compactioning_set_print(victim->seg_idx);
							gc_sptr_node_free(gsn);
						}
					}
					sptr=lsmtree_find_target_normal_sst_datagc(gn->lba, gn->piece_ppa, &level_idx, &target_version, &sptr_idx);
					if(sptr && sptr->type==PAGE_FILE){
						if(sptr->file_addr.map_ppa==gn->piece_ppa/L2PGAP){
							if(gn->piece_ppa%L2PGAP==0){
								gmc=gmc_init(gn, sptr, target_version);
								gmc->is_page_file_map=true;
								gc_page_file_map->insert(std::pair<uint32_t, gc_mapping_check_node*>(gmc->lba, gmc));
								if(sptr->file_addr.map_ppa==debug_piece_ppa/L2PGAP){
									printf("target_sptr:%u~%u %u, lba:%u\n", sptr->start_lba, sptr->end_lba, sptr->file_addr.map_ppa, gn->lba);
								}
							}
						}
						else{
							recent_version=version_map_lba(LSM.last_run_version, oob_lba[i]);
							gmc=gmc_init(gn, sptr, target_version);
							if(recent_version!=target_version){
								gmc->new_piece_ppa=UINT32_MAX;
								gmc->is_invalidate_node=true;
								gc_page_file_invalid_kv_map->insert(std::pair<uint32_t, gc_mapping_check_node*>(gmc->lba, gmc));
							}
							else{
								if(!page_file_wb){
									page_file_wb=write_buffer_init(_PPS*L2PGAP, pm, GC_WB);	
								}
								write_buffer_insert_for_gc(page_file_wb, gmc->lba, gmc->data_ptr);
								gc_page_file_kv_map->insert(std::pair<uint32_t, gc_mapping_check_node*>(gmc->lba, gmc));
							}
						}
						sptr=NULL;
						continue;
					}
					if(sptr){
						for(uint32_t temp_idx=0; temp_idx<sptr->map_num; temp_idx++){
							if(sptr->block_file_map[temp_idx].ppa/_PPS!=victim->seg_idx){
								EPRINT("wtf", true);
							}
							invalidate_map_ppa(bm, sptr->block_file_map[temp_idx].ppa, true);
						}
						gsn=gc_sptr_node_init(sptr, valid_piece_ppa_num, level_idx, target_version, sptr_idx);
					}
					else{
						gsn=NULL;
					}
				}

				/*for direct mapping*/
				if(!sptr || 
						(sptr && (((!sptr->sequential_file && sptr->end_ppa*L2PGAP<piece_ppa ) || (sptr->sequential_file && sptr->seq_data_end_piece_ppa<piece_ppa))
								  && !is_map_ppa(sptr, piece_ppa/L2PGAP))))
				{
					if(oob_lba[i]==debug_lba){
						printf("debug hit break! at %u\n", piece_ppa);
					}
					/*filtering invalid data*/
					recent_version=version_map_lba(LSM.last_run_version, oob_lba[i]);
					if(recent_version==UINT8_MAX){ //the version is in direct mapping
						std::map<uint32_t, uint32_t>::iterator find_iter;
						if(LSM.flushed_kp_set){
							find_iter=LSM.flushed_kp_set->find(oob_lba[i]);
							if(find_iter!=LSM.flushed_kp_set->end() &&
									find_iter->second!=piece_ppa){
								invalidate_kp_entry(oob_lba[i], piece_ppa, UINT32_MAX, true);
								continue;
							}
						}
#ifdef WB_SEPARATE
						if(LSM.hot_kp_set){
							find_iter=LSM.hot_kp_set->find(oob_lba[i]);
							if(find_iter!=LSM.hot_kp_set->end() &&
									find_iter->second!=piece_ppa){
								invalidate_kp_entry(oob_lba[i], piece_ppa, UINT32_MAX, true);
								continue;
							}
						}
#endif
						if(LSM.flushed_kp_temp_set){
							find_iter=LSM.flushed_kp_temp_set->find(oob_lba[i]);
							if(find_iter!=LSM.flushed_kp_temp_set->end() &&
									find_iter->second!=piece_ppa){
								invalidate_kp_entry(oob_lba[i], piece_ppa, UINT32_MAX, true);
								continue;
							}
						}
					}
					else{
#ifdef MIN_ENTRY_PER_SST
						if(LSM.unaligned_sst_file_set && LSM.unaligned_sst_file_set->now_sst_num){
							uint32_t idx=run_retrieve_sst_idx(LSM.unaligned_sst_file_set, oob_lba[i]);
							if(idx!=UINT32_MAX){
								sst_file *unaligned_sptr=&LSM.unaligned_sst_file_set->sst_set[idx];
								if(unaligned_sptr->type==PAGE_FILE){
									if(unaligned_sptr->file_addr.map_ppa!=piece_ppa/L2PGAP){
										EPRINT("can't be", true);
									}
								}
								else{
									if(unaligned_sptr->block_file_map[0].ppa!=piece_ppa/L2PGAP){
										EPRINT("can't be", true);
									}
								}
								invalidate_sst_file_map(unaligned_sptr);
								run_remove_sst_file_at(LSM.unaligned_sst_file_set, idx);
								if(LSM.unaligned_sst_file_set->now_sst_num==0){
									run_free(LSM.unaligned_sst_file_set);
									LSM.unaligned_sst_file_set=NULL;
								}
								continue;
							}
						}
#endif
						printf("lba:%u piece_ppa:%u ", oob_lba[i], piece_ppa);
						EPRINT("???\n", true);
					}

				//	lsmtree_level_summary(&LSM);
				//	uint32_t temp_remain_page=page_manager_get_total_remain_page(LSM.pm, false, true);
				//	lsmtree_seg_debug(&LSM);
				//	printf("remain total_page:%u\n", temp_remain_page);
				//	EPRINT("should I implement?", true);
				
					/*checking other level*/
					gmc=gmc_init(gn, NULL, UINT32_MAX);
					if(!wisckey_node_wb){
						wisckey_node_wb=write_buffer_init(_PPS*L2PGAP, pm, GC_WB);	
					}
					write_buffer_insert_for_gc(wisckey_node_wb, gmc->lba, gmc->data_ptr);
					gc_kv_map->insert(std::pair<uint32_t, gc_mapping_check_node*>(gmc->lba, gmc));

					if(gsn){
						gc_sptr_node_free(gsn);
					}
				}
				else{
					//sptr->trimed_sst_file=true;
					if(is_map_ppa(sptr, piece_ppa/L2PGAP)){
						invalidate_map_ppa(pm->bm, piece_ppa/L2PGAP, true);
						/*mapaddress*/
						continue;
					}

					if(sptr->end_ppa/_PPS != victim->seg_idx){
						EPRINT("target sptr should be in victim", true);
					}

					recent_version=version_map_lba(LSM.last_run_version, oob_lba[i]);

					/*should figure out map ppa*/
					if(version_compare(LSM.last_run_version, recent_version, target_version)<=0){
						invalidate_kp_entry(gn->lba, gn->piece_ppa, UINT32_MAX, true);
						insert_target_sptr(gsn, gn->lba, &gn->data->value[LPAGESIZE*i]);
					}
					else{
						gsn->early_invalidation_cnt++;
						updating_now_compactioning_data(target_version, victim->seg_idx, gn->lba, UINT32_MAX);
						invalidate_kp_entry(gn->lba, gn->piece_ppa, UINT32_MAX, true);
						continue; //already invalidate
					}
				}
			}
		}

		free_target[q_idx++]=gn->data;
		gc_read_node_free(gn);
		gc_target_queue->pop();
	}
	if(sptr){
		if(gsn->wb->buffered_entry_num){
			move_sptr(gsn,victim->seg_idx, gsn->lev_idx, gsn->version, gsn->sidx);
		}
		else{	
			uint32_t ridx=version_to_ridx(LSM.last_run_version, gsn->lev_idx, gsn->version);
			level_contents_num_updates_at_gc(LSM.disk[gsn->lev_idx], 
					ridx, read_helper_get_cnt(gsn->sptr->_read_helper));
			version_decrease_invalidation_number(LSM.last_run_version, 
					gsn->version, gsn->early_invalidation_cnt);
			
			level_sptr_remove_at_in_gc(LSM.disk[gsn->lev_idx], 
					version_to_ridx(LSM.last_run_version, gsn->lev_idx, gsn->version), 
					gsn->sidx);
			gc_sptr_node_free(gsn);
		}
	}

	gc_helper_for_direct_mapping(gc_kv_map, wisckey_node_wb, victim->seg_idx);
	gc_helper_for_page_file(gc_page_file_kv_map, gc_page_file_map, 
			gc_page_file_invalid_kv_map, page_file_wb, victim->seg_idx);
	

	fdriver_lock(&LSM.now_gc_seg_lock);
	LSM.now_gc_seg_idx=victim->seg_idx;
	fdriver_unlock(&LSM.now_gc_seg_lock);

	uint32_t victim_seg_idx=victim->seg_idx;
	bm->trim_segment(bm, victim);

	lsmtree_compactioning_set_gced_flag(victim_seg_idx);

	page_manager_change_reserve(pm, false);

	for(uint32_t i=0; i<q_idx; i++){
		inf_free_valueset(free_target[i], FS_MALLOC_R);
	}
	free(free_target);

	delete gc_target_queue;
	delete gc_kv_map;
	delete gc_page_file_kv_map;
	delete gc_page_file_map;
	delete gc_page_file_invalid_kv_map;

	if(wisckey_node_wb){
		write_buffer_free(wisckey_node_wb);
	}
	if(page_file_wb){
		write_buffer_free(page_file_wb);
	}
	return false;
}

void page_manager_insert_remain_seg(page_manager *pm, __segment *seg){
	pm->remain_data_segment_q->push_back(seg);
}
