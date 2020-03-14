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
	
	p->reserve=page_ftl.bm->get_segment(page_ftl.bm,true);
	p->active=page_ftl.bm->get_segment(page_ftl.bm,false);
	page_ftl.algo_body=(void*)p;
}

uint32_t page_map_assign(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	if(page_ftl.bm->check_full(page_ftl.bm, p->active,MASTER_PAGE) && page_ftl.bm->is_gc_needed(page_ftl.bm)){
		do_gc();
	}

	if(p->mapping[lba]!=UINT_MAX){
		invalidate_ppa(p->mapping[lba]);
	}
retry:
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->active);
	if(res==UINT_MAX){
		p->active=page_ftl.bm->get_segment(page_ftl.bm,false);
		goto retry;
	}

	validate_ppa(res,lba);
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
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve);
	invalidate_ppa(ppa);
	validate_ppa(res,lba);
	return res;
}

void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
}
