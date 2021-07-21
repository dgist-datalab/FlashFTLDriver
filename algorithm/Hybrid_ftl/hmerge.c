#include "../../include/container.h"
#include "hybridmap.h"
#include "hmerge.h"
#include "../../include/data_struct/list.h"


extern algorithm hybrid_ftl;
merge_buffer m_buffer;

void hybrid_merge(uint32_t lbnum){
    uint32_t lbn;
    blockmanager *bm = hybrid_ftl.bm;
    hm_body*h = (hm_body*)hybrid_ftl.algo_body;

    lbn = h->logblock[lbnum].db_idx;
    
    bool sw = true;    

    for(int i=0;i<_LPPS-1;i++) {
        if (h->logblock[lbnum].lbmapping[i] == -1 || (h->logblock[lbnum].lbmapping[i]> h->logblock[lbnum].lbmapping[i+1])) { 
            sw = false;
            break;
        }
    }


   if(sw)
        do_switch(lbnum);
   else
        do_merge(lbnum);
	
}


#if 1

void do_switch(uint32_t lbnum){
    uint32_t lbn;

    blockmanager* bm = hybrid_ftl.bm;
    hm_body*h = (hm_body*)hybrid_ftl.algo_body;
    lbn = h->logblock[lbnum].db_idx;   	

    if(h->datablock[lbn].pba != -1){
   	 bm->trim_target_segment(bm, h->datablock[lbn].pdb, hybrid_ftl.li); 	 
    }

    h->datablock[lbn].pba = h->logblock[lbnum].lbmapping[0];
    h->datablock[lbn].pdb = h->logblock[lbnum].plb;       
}


void do_merge(uint32_t lbnum){
    uint32_t lbn, page, bidx, pidx;    
    align_gc_buffer  g_buffer;
       
    blockmanager* bm = hybrid_ftl.bm;
    hm_body*h = (hm_body*)hybrid_ftl.algo_body; 
    lbn = h->logblock[lbnum].db_idx;    
    gc_value * gv;


    __segment * merge = hybrid_ftl.bm->get_segment(bm, true);   
    list * temp_list = list_init();

    

    __segment *target = h->datablock[lbn].pdb;

    if(h->datablock[lbn].pba != -1){ //when datablock is not  empty	
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
    }

    target = h->logblock[lbnum].plb;

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


    KEYT*lbas;
    li_node * now, *nxt;
    while(temp_list ->size){
	for_each_list_node_safe(temp_list, now,nxt){
		gv = (gc_value*)now->data;
		if(!gv->isdone) continue;

		lbas = (KEYT*)bm->get_oob(bm, gv->ppa);

		for(uint32_t i=0;i<L2PGAP;i++){
			uint32_t offset = lbas[i] %(_LPPS);
			if(bm->is_invalid_page(bm, gv->ppa*L2PGAP+i)) continue;

			memcpy(&m_buffer.value[offset * LPAGESIZE], &gv->value->value[i*LPAGESIZE], LPAGESIZE);
			m_buffer.key[offset] = lbas[i];
		}

	inf_free_valueset(gv->value, FS_MALLOC_R);
	free(gv);
	list_delete_node(temp_list, now);		
	}
    }
    list_free(temp_list);	


    uint32_t res;
    bool first= true;
    uint32_t pba;
    for(int k=0;k<_PPS;k++){
	for(int i=0;i<L2PGAP;i++){
		memcpy(&g_buffer.value[i* LPAGESIZE], &m_buffer.value[k*PAGESIZE+i*LPAGESIZE], LPAGESIZE);
		g_buffer.key[i] = m_buffer.key[k*L2PGAP + i];
	}

	res = hybrid_ftl.bm->get_page_num(hybrid_ftl.bm, merge);	
	if(first){
		pba = res;
		first = false;
	}
	validate_ppa(res,g_buffer.key,L2PGAP);
	send_req(res, GCDW,inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
    }

    

    if(h->datablock[lbn].pba != -1){
	    bm->trim_target_segment(bm, h->datablock[lbn].pdb, hybrid_ftl.li);	   	    
    }
    bm->trim_target_segment(bm, h->logblock[lbnum].plb, hybrid_ftl.li);
    
    
    h->datablock[lbn].pba = pba;
    h->datablock[lbn].lb_idx = -1;
    h->datablock[lbn].pdb = merge;
    
    for(int i=0;i<_LPPS;i++){
	h->logblock[lbnum].lbmapping[i] =  -1;
    }
    h->logblock[lbnum].empty =true;
    h->logblock[lbnum].cnt = 0;   
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

#endif

