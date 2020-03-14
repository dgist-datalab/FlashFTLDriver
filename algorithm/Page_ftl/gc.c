#include "gc.h"
#include "map.h"
#include "../../include/data_struct/list.h"
#include <stdlib.h>

extern algorithm page_ftl;
void invalidate_ppa(uint32_t ppa){
	page_ftl.bm->unpopulate_bit(page_ftl.bm,ppa);
}

void validate_ppa(uint32_t ppa, uint32_t lba){
	page_ftl.bm->populate_bit(page_ftl.bm,ppa);
	page_ftl.bm->set_oob(page_ftl.bm,(char*)&lba,sizeof(lba),ppa);
}

gc_value* send_req(uint32_t ppa, uint8_t type, gc_value *input){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=NULL;
	my_req->end_req=page_gc_end_req;
	my_req->type=type;
	
	gc_value *res=NULL;
	switch(type){
		case GCDR:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->isdone=false;
			res->ppa=ppa;
			my_req->params=(void *)res;
			my_req->type_lower=0;
			res->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			page_ftl.li->read(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
		case GCDW:
			res=input;
			my_req->params=(void *)res;
			page_ftl.li->write(ppa,PAGESIZE,input->value,ASYNC,my_req);
			break;
	}
	return res;
}

void do_gc(){
	__gsegment *target=page_ftl.bm->get_gc_target(page_ftl.bm);
//	printf("call gc!\n");
	uint32_t page;
	uint32_t bidx, pidx;
	blockmanager *bm=page_ftl.bm;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	list *temp_list=list_init();
	gc_value *gv;
	for_each_page_in_seg(target,page,bidx,pidx){
		if(bm->is_invalid_page(bm,page)) continue;
		gv=send_req(page,GCDR,NULL);
		list_insert(temp_list,(void*)gv);
	}

	li_node *now,*nxt;
	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){
			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
			uint32_t lba=*(uint32_t*)bm->get_oob(bm,gv->ppa);
			send_req(page_map_gc_update(lba,gv->ppa),GCDW,gv);
			list_delete_node(temp_list,now);
		}
	}

	bm->trim_segment(bm,target,page_ftl.li);
	p->active=p->reserve;
	p->reserve=bm->change_reserve(bm,p->reserve);
	list_free(temp_list);
}

void *page_gc_end_req(algo_req *input){
	gc_value *gv=(gc_value*)input->params;
	int cnt=0;
	switch(input->type){
		case GCDR:
			gv->isdone=true;
			break;
		case GCDW:
			inf_free_valueset(gv->value,FS_MALLOC_R);
			free(gv);
			break;
	}
	free(input);
	return NULL;
}
