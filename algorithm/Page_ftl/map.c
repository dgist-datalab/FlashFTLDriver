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

uint32_t page_map_assign(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;

	if(lba>_NOP){
		printf("over max page!!\n");
		abort();
	}

	/*you can check if the gc is needed or not, using this condition*/
	if(page_ftl.bm->check_full(page_ftl.bm, p->active,MASTER_PAGE) && page_ftl.bm->is_gc_needed(page_ftl.bm)){
		do_gc();//call gc
	}

	if(p->mapping[lba]!=UINT_MAX){
	/*when mapping was updated, the old one is checked as a inavlid*/
		invalidate_ppa(p->mapping[lba]);
	}

retry:
	/*get a page by bm->get_page_num, when the active block doesn't have block, return UINT_MAX*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->active);

	if(res==UINT_MAX){
		p->active=page_ftl.bm->get_segment(page_ftl.bm,false); //get a new block
		goto retry;
	}

	/*validate a page*/
	validate_ppa(res,lba);

	/*mapping update*/
	p->mapping[lba]=res;
	return res;
}

uint32_t page_map_pick(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	
	if(res==UINT_MAX) abort();
	return res;
}

uint32_t page_map_gc_update(uint32_t lba, uint32_t ppa){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;

	/*when the gc phase, It should get a page from the reserved block*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve);
	p->mapping[lba]=res;

	invalidate_ppa(ppa);
	validate_ppa(res,lba);
	return res;
}

void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
}
