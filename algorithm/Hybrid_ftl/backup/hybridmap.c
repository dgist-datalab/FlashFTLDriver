#include "../../include/container.h"
#include "../../include/settings.h"
#include "hybrid.h"
#include "hybridmap.h"
#include "hmerge.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>


extern algorithm hybrid_ftl;

void hybrid_map_create(){
    hm_body * h=(hm_body*)calloc(sizeof(hm_body),1);
    h->datablock = (db_t*)malloc(sizeof(db_t)*_NOS); //data block table
    h->logblock = (lb_t*)malloc(sizeof(lb_t)* _NOLB); // log block table

    /*init datablock*/
    for(int i=0;i<_NOS;i++){
	h->datablock[i].pba = -1;
        h->datablock[i].lb_idx = -1;
    }

    /*init logblock*/
    for(int i=0;i<_NOLB;i++){
	    for(int j=0;j<_LPPS;j++){
		    h->logblock[i].lbmapping[j] = -1;
	    }
	    
	h->logblock[i].empty = true;
	h->logblock[i].cnt = 0;
    }
 

    hybrid_ftl.algo_body=(void*)h; //you can assign your data structure in algorithm structure
}

uint32_t find_empty_lb(){
    hm_body * h = (hm_body*)hybrid_ftl.algo_body;
    uint32_t lbnum;
    for(int i=0;i<_NOLB;i++){
        if(h->logblock[i].empty){
	    h->logblock[i].empty = false;
            lbnum = i;
            return lbnum;
        }
    }
    lbnum = -1; //log block full -> merge call!
    return lbnum;
}


void validate_ppa(uint32_t ppa, KEYT *lbas, uint32_t max_idx){
	/*when the ppa is validated this function must be called*/
	for(uint32_t i=0; i<max_idx; i++){
		hybrid_ftl.bm->populate_bit(hybrid_ftl.bm,ppa * L2PGAP+i);
	}

	/*this function is used for write some data to OOB(spare area) for reverse mapping*/
	hybrid_ftl.bm->set_oob(hybrid_ftl.bm,(char*)lbas,sizeof(KEYT)*max_idx,ppa);
}


void invalidate_ppa(uint32_t t_ppa){
	hybrid_ftl.bm -> unpopulate_bit(hybrid_ftl.bm, t_ppa);
}

uint32_t hybrid_map_assign(KEYT* lba, uint32_t max_idx){
    hm_body *h=(hm_body*)hybrid_ftl.algo_body;
    blockmanager *bm = hybrid_ftl.bm;
    int target_lb,new_target_lb, mer_lb;
    uint32_t res, cnt;
    uint32_t ppn, lbn, offset;
    uint32_t max= 0;

    lbn = lba[0] / _LPPS;
    target_lb = h->datablock[lbn].lb_idx;

    if(target_lb == -1){
           new_target_lb = find_empty_lb();
           if(new_target_lb == -1){ // when extra log block doesn't exist
               for(int i=0;i<_NOLB;i++){
                   if(h->logblock[i].cnt > max) {
                       max = h->logblock[i].cnt;
                       mer_lb = i;
                   }
               }
	       
               hybrid_merge(mer_lb);
               new_target_lb = mer_lb;
	       h->logblock[new_target_lb].empty = false;

           }
	   
            h->logblock[new_target_lb].plb = bm->get_segment(bm, true);
	    
            h->datablock[lbn].lb_idx = new_target_lb;
            h->logblock[new_target_lb].db_idx = lbn;

            ppn = bm->get_page_num(bm, h->logblock[new_target_lb].plb);
	    validate_ppa(ppn,lba,L2PGAP);	   

            for(uint32_t i=0;i<L2PGAP;i++){
                offset = lba[i] % (_LPPS);
                
		if(h->datablock[lbn].pba != -1){
			if(bm->is_valid_page(bm,h->datablock[lbn].pba+offset))
				invalidate_ppa(h->datablock[lbn].pba+offset);
		}

                h->logblock[new_target_lb].lbmapping[offset] = ppn*L2PGAP + i;
                h->logblock[new_target_lb].cnt++;
            }
            res = ppn;
        }
        else{
            ppn = bm->get_page_num(bm, h->logblock[target_lb].plb);
    	    validate_ppa(ppn,lba,L2PGAP);
            for(uint32_t i=0;i<L2PGAP;i++){
                offset = lba[i] % (_LPPS);
	
		if(h->logblock[target_lb].lbmapping[offset]!= -1)
	                invalidate_ppa(h->logblock[target_lb].lbmapping[offset]);
		if(h->datablock[lbn].pba != -1){			
			if(bm->is_valid_page(bm,h->datablock[lbn].pba+offset))
				invalidate_ppa(h->datablock[lbn].pba+offset);
		}
                h->logblock[target_lb].lbmapping[offset] = ppn*L2PGAP + i;		
                h->logblock[target_lb].cnt++;
            }

            if(bm->check_full(bm,h->logblock[target_lb].plb, MASTER_BLOCK)){ //if block full -> merge call!		
                hybrid_merge(target_lb);
            }
            res = ppn;
        }
    return res;
}

uint32_t hybrid_map_pick(uint32_t lba) {
    hm_body *h=(hm_body*)hybrid_ftl.algo_body;
    blockmanager *bm = hybrid_ftl.bm;
    uint32_t lbn = lba / (_LPPS);
    uint32_t offset = lba % (_LPPS);
    uint32_t target_lb = h->datablock[lbn].lb_idx;

    if(bm->is_valid_page(bm,h->logblock[target_lb].lbmapping[offset])){
        return h->logblock[target_lb].lbmapping[offset];
    }
    return h->datablock[lbn].pba + offset;
}
