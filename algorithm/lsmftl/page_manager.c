#include "page_manager.h"
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>

void validate_piece_ppa(blockmanager *bm, uint32_t piece_num, uint32_t *piece_ppa,
		uint32_t *lba){
	for(uint32_t i=0; i<piece_num; i++){
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
	bm->unpopulate_bit(bm, piece_ppa);
}

page_manager* page_manager_init(struct blockmanager *_bm){
	page_manager *pm=(page_manager*)calloc(1,sizeof(page_manager));
	pm->bm=_bm;
	pm->current_segment[DATA_S]=_bm->get_segment(_bm, false);
	pm->seg_type_checker[pm->current_segment[DATA_S]->seg_idx]=DATASEG;
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

uint32_t page_manager_get_new_ppa(page_manager *pm, bool is_map){
	uint32_t res;
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];

	if(bm->check_full(bm, seg, MASTER_PAGE)){
		if(bm->is_gc_needed(bm)){
			EPRINT("before get ppa, try to gc!!\n", true);
			do_gc(pm);
		}
		free(seg);
		pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=is_map?MAPSEG:DATASEG;
		goto retry;
	}
	res=bm->get_page_num(bm, seg);
	return res;
}

uint32_t page_manager_pick_new_ppa(page_manager *pm, bool is_map){
	blockmanager *bm=pm->bm;
retry:
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(bm->check_full(bm, seg, MASTER_PAGE)){
		if(bm->is_gc_needed(bm)){
			EPRINT("before get ppa, try to gc!!\n", true);
			do_gc(pm);
		}
		free(seg);
		pm->current_segment[is_map?MAP_S:DATA_S]=bm->get_segment(bm,false);
		pm->seg_type_checker[pm->current_segment[is_map?MAP_S:DATA_S]->seg_idx]=is_map?MAPSEG:DATASEG;
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
