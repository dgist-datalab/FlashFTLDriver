#include "map.h"
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

extern algorithm page_ftl;

void page_map_create(){
	pm_body *p=(pm_body*)calloc(sizeof(pm_body),1);
	p->mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP);
	for(int i=0;i<_NOP; i++){
		p->mapping[i]=UINT_MAX;
	}
	
	p->reserve=page_ftl.bm->get_segment(page_ftl.bm,true); //reserve for GC
	p->active=page_ftl.bm->get_segment(page_ftl.bm,false); //now active block for inserted request.
	page_ftl.algo_body=(void*)p; //you can assign your data structure in algorithm structure
}

uint32_t page_map_assign(KEYT* lba){
	uint32_t res=0;

	res=get_ppa(lba);
	pm_body *p=(pm_body*)page_ftl.algo_body;
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			invalidate_ppa(p->mapping[t_lba]);
		}
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
	}

	return res;
}

uint32_t page_map_pick(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	
	if(res==UINT_MAX) abort();
	return res;
}

uint32_t page_map_gc_update(KEYT *lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;

	/*when the gc phase, It should get a page from the reserved block*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve);
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			invalidate_ppa(p->mapping[t_lba]);
		}
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
	}

	return res;
}

void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
}
