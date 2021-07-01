#include "map.h"
#include "gc.h"
#include "page.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
extern uint32_t test_key;
extern algorithm page_ftl;
#if 1 //NAM
unsigned char dirty_option=0x1; 
unsigned char node_dirty_option=0x2; 
unsigned char heap_option=0x4; 
#endif 

void page_map_create(){
	pm_body *p=(pm_body*)calloc(sizeof(pm_body),1);
	p->mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP*L2PGAP);
	for(int i=0;i<_NOP*L2PGAP; i++){
		p->mapping[i]=UINT_MAX;
	}

#if 1 //NAM
	p->dirty_check=(unsigned char*)malloc(sizeof(unsigned char)*_DCE); 
	for(int i=0; i<_DCE; i++){ 
		p->dirty_check[i]=0; 
	} 
	
	p->tot_dirty_pages = 0; 
	p->tot_flush_count = 0; 
	p->mapflush=page_ftl.bm->get_segment(page_ftl.bm,false); //add the other active block for inserted mapping
#endif	
	p->reserve=page_ftl.bm->get_segment(page_ftl.bm,true); //reserve for GC
	p->active=page_ftl.bm->get_segment(page_ftl.bm,false); //now active block for inserted request.
	page_ftl.algo_body=(void*)p; //you can assign your data structure in algorithm structure
}

#if 1 //NAM
int32_t page_dMap_check(KEYT lba){
	uint32_t fidx = lba >> _PMES; 
	pm_body *p=(pm_body*)page_ftl.algo_body; 

	if(p->dirty_check[fidx] & dirty_option)
		return 0; 

	p->dirty_check[fidx] |= dirty_option; 
	p->tot_dirty_pages++; 
	
	if(p->tot_dirty_pages >= MAX_PROTECTED){ 
		page_map_flush(); 
	}
	
	return 0;  
} 
#endif

#if 1 //NAM
int32_t page_map_flush(){ 
	pm_body *p=(pm_body*)page_ftl.algo_body; 

#if 1 //NAM
	for(uint32_t i=0; i<_DCE; i++){
		if(p->dirty_check[i] & dirty_option){ 
			ppa_t ppa=get_ppa_mapflush(); 
			value_set *value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE); 
			memcpy(&value->value[0], &p->mapping[i*8192], 8192); 
			send_user_req(NULL, DATAW, ppa, value); 
			p->dirty_check[i] &= ~dirty_option;
		}
	} 
#endif	
	p->tot_dirty_pages = 0; 
	p->tot_flush_count++;

	return 0;  	
} 
#endif

uint32_t page_map_assign(KEYT* lba, uint32_t max_idx){
	uint32_t res=0;

	res=get_ppa(lba, L2PGAP);
	pm_body *p=(pm_body*)page_ftl.algo_body;
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		uint32_t previous_ppa=p->mapping[t_lba];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			invalidate_ppa(p->mapping[t_lba]);
		}
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
		if(t_lba==test_key){

		}
	//	DPRINTF("\tmap set : %u->%u\n", t_lba, p->mapping[t_lba]);
	}
	page_dMap_check(lba[0]);
 
	return res;
}

uint32_t page_map_pick(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	return res;
}


uint32_t page_map_trim(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	if(res==UINT32_MAX){
		return 0;
	}
	else{
		invalidate_ppa(res);
		p->mapping[lba]=UINT32_MAX;
		return 1;
	}
}

uint32_t page_map_gc_update(KEYT *lba, uint32_t idx){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;

	/*when the gc phase, It should get a page from the reserved block*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve);
	uint32_t old_ppa, new_ppa;
	for(uint32_t i=0; i<idx; i++){
		KEYT t_lba=lba[i];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			//invalidate_ppa(p->mapping[t_lba]);
		}
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
		if(t_lba==test_key){

		}
	}

/*
	for(uint32_t i=idx; i<L2PGAP; i++){
		invalidate_ppa(res*L2PGAP+idx);
	}
*/
	return res;
}

void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
}


