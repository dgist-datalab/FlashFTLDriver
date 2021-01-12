#include "page_manager.h"
#include <stdlib.h>
#include <stdio.h>

_page_manager* page_manager_init(bool is_master, char **map_data, struct blockmanager *_bm){
	_page_manater *pm=(_page_manager*)calloc(1,sizeof(page_manager));
	pm->is_master_page_manager=is_master;
	pm->current_lba_map_ptr=0;
	pm->bm=_bm;
	if(is_master){
		pm->current_block=_bm->get_segment(_bm, false);
		pm->reserve_block=_bm->get_segment(_bm, true);
	}
	else{
		if(!map_data){
			EPRINT("NullPtr for map!", true);
		}
		pm->lba_map.ptr=map_data;
	}
	return pm;
}

void page_manager_insert_lba(_page_manager *pm, uint32_t lba){

}

void page_manager_free(_page_manager* pm){
	
}

uint32_t page_mangager_get_ppa(_page_manager *pm){

}
