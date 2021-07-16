#include "../../include/container.h"
#include "../../include/data_struct/list.h"
#include "hybridmap.h"
#include "hmerge.h"
#include <stdlib.h>
#include <stdint.h>
extern algorithm hybrid_ftl;


void hybrid_merge(lb_t logblock){
    
    blockmanager *bm = hybrid_ftl.bm;
    bool sw = true;
    for(int i=0;i<_PPS-1;i++) {
        if (logblock.lbmapping[i] == -1||(logblock.lbmapping[i]>logblock.lbmapping[i+1])) { 
            sw = false;
            break;
        }
    }


   if(sw)
        do_switch(logblock);
    else
        do_merge(logblock);
}




void do_switch(lb_t logblock){
    uint32_t lbn;
    blockmanager* bm = hybrid_ftl.bm;
    hm_body*h = (hm_body*)hybrid_ftl.algo_body;
    lbn = logblock.db_idx;

    bm->trim_target_segment(bm, h->datablock[lbn].pdb, hybrid_ftl.li);
    bm->free_segment(bm, h->datablock[lbn].pdb);


    h->datablock[lbn].pba = logblock.lbmapping[0];
    h->datablock[lbn].pdb = logblock.plb;
    logblock.plb = NULL;   
}

void do_merge(lb_t logblock){
    printf("\ndo_merge");
    uint32_t lbn, page;
    value_set * value;
    blockmanager* bm = hybrid_ftl.bm;
    hm_body*h = (hm_body*)hybrid_ftl.algo_body;                            
    lbn = logblock.db_idx;      
    __segment * merge = hybrid_ftl.bm->get_segment(bm, false);    
    page = h->datablock[lbn].pba;
    merge_buffer m_buffer;
    align_gc_buffer g_buffer;
    list *temp_list=list_init();
    gc_value *gv;
    bool should_read = false;
    for(int i=0;i<_PPS;i++){
    	if(logblock.lbmapping[i]==-1){		
		for(uint32_t j=0; j<L2PGAP; j++){
			bool should_read = false;
			if(bm->is_invalid_page(bm,page+i*L2PGAP + j)) continue;
			else{
				should_read=true;
				break;
			}
		}
		if(should_read){
			gv=send_req(page,GCDR,NULL);
			list_insert(temp_list,(void*)gv);
		}else{
		}
    	}
	else{
		gv = send_req(logblock.lbmapping[i],GCDR,NULL);
		list_insert(temp_list,(void*)gv);
	}
    li_node *now, *nxt;
    m_buffer.idx=0;
    KEYT *lbas;
    while(temp_list->size){
        for_each_list_node_safe(temp_list, now,nxt){
            gv = (gc_value*)now->data;
            if(!gv->isdone) continue;
            lbas = (KEYT*)bm->get_oob(bm, gv->ppa);
            for(uint32_t i=0;i<L2PGAP;i++){
                    uint32_t offset = lbas[i] %(_PPS * L2PGAP);
                    if(bm->is_invalid_page(bm, gv->ppa*L2PGAP+i)) continue;
                    memcpy(&m_buffer.value[offset * LPAGESIZE], &gv->value->value[i*LPAGESIZE], LPAGESIZE);
                    m_buffer.key[offset] = lbas[i];
            }

        inf_free_valueset(gv->value, FS_MALLOC_R);
        free(gv);
        list_delete_node(temp_list, now);
        }
    }

    uint32_t res;

    for(int k=0;k<_PPS;k++){
        for(int i=0;i<L2PGAP;i++){
                memcpy(&g_buffer.value[i* LPAGESIZE], &m_buffer.value[i*LPAGESIZE], LPAGESIZE);
                g_buffer.key[i] = m_buffer.key;

        }
        res = hybrid_ftl.bm->get_page_num(hybrid_ftl.bm, merge);
        send_req(res, GCDW,inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
    }




    bm->trim_target_segment(bm, h->datablock[lbn].pdb, hybrid_ftl.li);
    bm->trim_target_segment(bm, logblock.plb, hybrid_ftl.li);
    bm->free_segment(bm, logblock.plb);
    }
}

gc_value* send_req(uint32_t ppa, uint8_t type, value_set *value){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=NULL;
	my_req->end_req=hybrid_merge_end_req;//call back function for GC
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
			hybrid_ftl.li->read(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
		case GCDW:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->value=value;
			my_req->param=(void *)res;
			hybrid_ftl.li->write(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
	}
	return res;
}





void *hybrid_merge_end_req(algo_req *input){
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
