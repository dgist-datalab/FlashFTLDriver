#include "page_manager.h"
#include <stdlib.h>
#include <stdio.h>

void validate_piece_ppa(blockmanager *bm, uint32_t piece_num, uint32_t *piece_ppa,
		uint32_t *lba){
	for(uint32_t i=0; i<piece_num; i++){
		char *oob=bm->get_oob(bm, PIECETOPPA(piece_ppa[i]));
		memcpy(&oob[(piece_ppa%2)*sizeof(uint32_t)], &lba[i], sizeof(uint32_t));
		bm->populate_bit(bm, piece_ppa[i]);
	}
}

void invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa){
	bm->unpopulate_bit(bm, piece_ppa);
}

page_manager* page_manager_init(struct blockmanager *bm){
	page_manager *pm=(page_manager*)calloc(1,sizeof(page_manager));
	pm->is_master_page_manager=is_master;
	pm->current_lba_map_ptr=0;
	pm->bm=_bm;
	if(is_master){
		pm->current_segment[DATA_S]=_bm->get_segment(_bm, false);
		pm->current_segment[MAP_S]=_bm->get_segment(_bm, false);
		pm->reserve_segment=_bm->get_segment(_bm, true);
	}
	else{
		if(!map_data){
			EPRINT("NullPtr for map!", true);
		}
		for(int i=0; i<num_of_map; i++){
			pm->lba_map.ptr[i]=(uint32_t*)map_data[i];
		}
	}
	return pm;
}

void page_manager_free(page_manager* pm){
	free(pm);
}

uint32_t page_manager_get_new_ppa(page_manager *pm, bool is_map){
	uint32_t res;
	blockmanager *bm=pm->bm;
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];

	if(bm->check_full(bm, seg, MASTER_PAGE) && bm->is_gc_needed(bm)){
		EPRINT("before get ppa, try to gc!!\n", true);
	}
	res=bm->get_page_num(bm, seg);
	return res;
}

bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page, 
		bool is_map){
	blockmanager *bm=pm->bm;
	__segment *seg=is_map?pm->current_segment[MAP_S] : pm->current_segment[DATA_S];
	if(seq->used_page_num + needed_page>= _PPS) return true;
	return bm->check_full(bm, pm->current_segment, MASTER_PAGE) && bm->is_gc_needed(bm); 
}
