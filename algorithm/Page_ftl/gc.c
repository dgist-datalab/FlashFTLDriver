#include "gc.h"
#include "map.h"
#include "../../include/data_struct/list.h"
#include <stdlib.h>
#include <stdint.h>

extern algorithm page_ftl;
void invalidate_ppa(uint32_t t_ppa){
	/*when the ppa is invalidated this function must be called*/
	page_ftl.bm->unpopulate_bit(page_ftl.bm, t_ppa);
}

void validate_ppa(uint32_t ppa, KEYT *lbas, uint32_t max_idx){
	/*when the ppa is validated this function must be called*/
	for(uint32_t i=0; i<max_idx; i++){
		page_ftl.bm->populate_bit(page_ftl.bm,ppa * L2PGAP+i);
	}

	/*this function is used for write some data to OOB(spare area) for reverse mapping*/
	page_ftl.bm->set_oob(page_ftl.bm,(char*)lbas,sizeof(KEYT)*max_idx,ppa);
}

gc_value* send_req(uint32_t ppa, uint8_t type, value_set *value){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=NULL;
	my_req->end_req=page_gc_end_req;//call back function for GC
	my_req->type=type;
	
	/*for gc, you should assign free space for reading valid data*/
	gc_value *res=NULL;
	switch(type){
		case GCDR:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->isdone=false;
			res->ppa=ppa;
			my_req->param=(void *)res;
			my_req->type_lower=0;
			/*when read a value, you can assign free value by this function*/
			res->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			page_ftl.li->read(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
		case GCDW:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->value=value;
			my_req->param=(void *)res;
			page_ftl.li->write(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
	}
	return res;
}

void new_do_gc(){
	/*this function return a block which have the most number of invalidated page*/
	__gsegment *target=page_ftl.bm->get_gc_target(page_ftl.bm);
	uint32_t page;
	uint32_t bidx, pidx;
	blockmanager *bm=page_ftl.bm;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	//list *temp_list=list_init();
	gc_value **gv_array=(gc_value**)malloc(sizeof(gc_value*)*_PPS);
	align_gc_buffer g_buffer;
	gc_value *gv;
	uint32_t gv_idx=0;

	/*by using this for loop, you can traversal all page in block*/
	for_each_page_in_seg(target,page,bidx,pidx){
		//this function check the page is valid or not
		gv=send_req(page,GCDR,NULL);
		gv_array[gv_idx++]=gv;
	}

	g_buffer.idx=0;
	KEYT *lbas;
	gv_idx=0;
	uint32_t done_cnt=0;
	while(done_cnt!=_PPS){
		gv=gv_array[gv_idx];
		if(!gv->isdone){
			goto next;
		}
		lbas=(KEYT*)bm->get_oob(bm, gv->ppa);
		for(uint32_t i=0; i<L2PGAP; i++){
			if(page_map_pick(lbas[i])!=gv->ppa*L2PGAP+i) continue;
			memcpy(&g_buffer.value[g_buffer.idx*LPAGESIZE],&gv->value->value[i*LPAGESIZE],LPAGESIZE);
			g_buffer.key[g_buffer.idx]=lbas[i];

			g_buffer.idx++;

			if(g_buffer.idx==L2PGAP){
				uint32_t res=page_map_gc_update(g_buffer.key, L2PGAP);
				validate_ppa(res, g_buffer.key, g_buffer.idx);
				send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
				g_buffer.idx=0;
			}
		}

		done_cnt++;

		inf_free_valueset(gv->value, FS_MALLOC_R);
		free(gv);
next:
		gv_idx=(gv_idx+1)%_PPS;
	}	

	if(g_buffer.idx!=0){
		uint32_t res=page_map_gc_update(g_buffer.key, g_buffer.idx);
		validate_ppa(res, g_buffer.key, g_buffer.idx);
		send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
		g_buffer.idx=0;	
	}

	bm->trim_segment(bm,target,page_ftl.li); //erase a block

	bm->free_segment(bm, p->active);

	p->active=p->reserve;//make reserved to active block
	p->reserve=bm->change_reserve(bm,p->reserve); //get new reserve block from block_manager
}

void do_gc(){
	/*this function return a block which have the most number of invalidated page*/
	__gsegment *target=page_ftl.bm->get_gc_target(page_ftl.bm);
	uint32_t page;
	uint32_t bidx, pidx;
	blockmanager *bm=page_ftl.bm;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	list *temp_list=list_init();
	align_gc_buffer g_buffer;
	gc_value *gv;

	//static int cnt=0;
	//printf("gc: %d\n", cnt++);
	/*by using this for loop, you can traversal all page in block*/
	for_each_page_in_seg(target,page,bidx,pidx){
		//this function check the page is valid or not
		bool should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm,page*L2PGAP+i)) continue;
			else{
				should_read=true;
				break;
			}
		}
		if(should_read){
			gv=send_req(page,GCDR,NULL);
			list_insert(temp_list,(void*)gv);
		}
	}

	li_node *now,*nxt;
	g_buffer.idx=0;
	KEYT *lbas;
	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){

			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
			lbas=(KEYT*)bm->get_oob(bm, gv->ppa);
			for(uint32_t i=0; i<L2PGAP; i++){
				if(bm->is_invalid_page(bm,gv->ppa*L2PGAP+i)) continue;
				memcpy(&g_buffer.value[g_buffer.idx*LPAGESIZE],&gv->value->value[i*LPAGESIZE],LPAGESIZE);
				g_buffer.key[g_buffer.idx]=lbas[i];

				g_buffer.idx++;

				if(g_buffer.idx==L2PGAP){
					uint32_t res=page_map_gc_update(g_buffer.key, L2PGAP);
					validate_ppa(res, g_buffer.key, g_buffer.idx);
					send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
					g_buffer.idx=0;
				}
			}

			inf_free_valueset(gv->value, FS_MALLOC_R);
			free(gv);
			//you can get lba from OOB(spare area) in physicall page
			list_delete_node(temp_list,now);
		}
	}

	if(g_buffer.idx!=0){
		uint32_t res=page_map_gc_update(g_buffer.key, g_buffer.idx);
		validate_ppa(res, g_buffer.key, g_buffer.idx);
		send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
		g_buffer.idx=0;	
	}

	bm->trim_segment(bm,target,page_ftl.li); //erase a block

	bm->free_segment(bm, p->active);

	p->active=p->reserve;//make reserved to active block
	p->reserve=bm->change_reserve(bm,p->reserve); //get new reserve block from block_manager

	list_free(temp_list);
}


ppa_t get_ppa(KEYT *lbas, uint32_t max_idx){
	uint32_t res;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	/*you can check if the gc is needed or not, using this condition*/
	if(page_ftl.bm->check_full(page_ftl.bm, p->active,MASTER_PAGE) && page_ftl.bm->is_gc_needed(page_ftl.bm)){
//		new_do_gc();//call gc
		do_gc();//call gc
	}

retry:
	/*get a page by bm->get_page_num, when the active block doesn't have block, return UINT_MAX*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->active);

	if(res==UINT32_MAX){
		page_ftl.bm->free_segment(page_ftl.bm, p->active);
		p->active=page_ftl.bm->get_segment(page_ftl.bm,false); //get a new block
		goto retry;
	}

	/*validate a page*/
	validate_ppa(res, lbas, max_idx);
	//printf("assigned %u\n",res);
	return res;
}

void *page_gc_end_req(algo_req *input){
	gc_value *gv=(gc_value*)input->param;
	switch(input->type){
		case GCDR:
			gv->isdone=true;
			break;
		case GCDW:
			/*free value which is assigned by inf_get_valueset*/
			inf_free_valueset(gv->value,FS_MALLOC_R);
			free(gv);
			break;
	}
	free(input);
	return NULL;
}
