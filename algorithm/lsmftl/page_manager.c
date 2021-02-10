#include "page_manager.h"
#include <stdlib.h>
#include <stdio.h>

static void validate_ppa(blockmanager *bm, uint32_t ppa){
	bm->populate_bit(bm, ppa);
}

static void invalidate_ppa(blockmanager *bm, uint32_t ppa){
	bm->unpopulate_bit(bm, ppa);
}

page_manager* page_manager_init(bool is_master, char **map_data, int num_of_map, struct blockmanager *_bm){
	page_manager *pm=(page_manager*)calloc(1,sizeof(page_manager));
	pm->is_master_page_manager=is_master;
	pm->current_lba_map_ptr=0;
	pm->bm=_bm;
	if(is_master){
		pm->current_segment=_bm->get_segment(_bm, false);
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

/*
void page_manager_insert_lba(page_manager *pm, uint32_t lba){
	if(pm->is_master_page_manager){
		uint32_t idx=pm->current_lba_map_ptr++;
		pm->lba_map.data[idx/MAX_MAP][idx%MAX_MAP]=lba;
	}
	else{
		EPRINT("it can't be", true);
	}
	return;
}
*/
void page_manager_free(page_manager* pm){
	free(pm);
}

uint32_t page_manager_get_new_ppa(page_manager *pm){
	if(!pm->is_master_page_manager){
		EPRINT("please check pm, it is not master", true);
		return -1;
	}
	uint32_t res;
	blockmanager *bm=pm->bm;
	if(bm->check_full(bm, pm->current_segment, MASTER_PAGE) && bm->is_gc_needed(bm)){
		EPRINT("before get ppa, try to gc!!\n", true);
	}
	res=bm->get_page_num(bm, pm->current_segment);
	return res;
}

bool page_manager_is_gc_needed(page_manager *pm, uint32_t needed_page){
	if(!pm->is_master_page_manager){
		EPRINT("please check pm, it is not master", true);
	}
	blockmanager *bm=pm->bm;
	return bm->check_full(bm, pm->current_segment, MASTER_PAGE) && bm->is_gc_needed(bm); 
}
