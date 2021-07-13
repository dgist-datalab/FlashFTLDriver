#include "../../include/container.h"
#include "../../include/settings.h"
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
    for(int i=0;i<_NOLB;i++){
        h->datablock[i].lb_idx = UINT_MAX;
    }

    hybrid_ftl.algo_body=(void*)h; //you can assign your data structure in algorithm structure
}

uint32_t find_empty_lb(){
    hm_body * h = (hm_body*)hybrid_ftl.algo_body;
    uint32_t lbnum;
    for(int i=0;i<_NOLB;i++){
        if(h->logblock[i].empty){
            lbnum = i;
            return lbnum;
        }
    }
    lbnum = -1; //log block full -> merge call!
    return lbnum;
}

void invalidate_ppa(uint32_t t_ppa){
	hybrid_ftl.bm -> unpopulate_bit(hybrid_ftl.bm, t_ppa);
}
uint32_t hybrid_map_assign(KEYT* lba, uint32_t max_idx){
    hm_body *h=(hm_body*)hybrid_ftl.algo_body;
    blockmanager *bm = hybrid_ftl.bm;
    uint32_t target_lb,new_target_lb, mer_lb;
    uint32_t res, cnt;
    uint32_t ppn, lbn, offset;
    uint32_t max= 0;

    lbn = lba[0]/_PPS;
    target_lb = h->datablock[lbn].lb_idx;

        if(target_lb == UINT_MAX){
           new_target_lb = find_empty_lb();
           if(new_target_lb == -1){ // when extra log block doesn't exist
               for(int i=0;i<_NOLB;i++){
                   if(h->logblock[i].cnt > max) {
                       max = h->logblock[i].cnt;
                       mer_lb = i;
                   }
               }
               hybrid_merge(h->logblock[mer_lb]);
               new_target_lb = mer_lb;
           }

            h->logblock[new_target_lb].plb = bm->get_segment(bm, false);

            h->datablock[lbn].lb_idx = new_target_lb;
            h->logblock[new_target_lb].db_idx = lbn;

            ppn = bm->get_page_num(bm, h->logblock[new_target_lb].plb);

            for(uint32_t i=0;i<L2PGAP;i++){
                offset = lba[i] % (_PPS);
                cnt = h->logblock[new_target_lb].cnt;

                h->logblock[new_target_lb].lbmapping[offset] = ppn*L2PGAP + i;
                h->logblock[new_target_lb].cnt++;
            }
            res = ppn;
        }
        else{
            ppn = bm->get_page_num(bm, h->logblock[target_lb].plb);

            for(uint32_t i=0;i<L2PGAP;i++){
                offset = lba[i] % (_PPS);

                invalidate_ppa(h->logblock[target_lb].lbmapping[offset]);

                h->logblock[target_lb].lbmapping[offset] = ppn*L2PGAP + i;
                h->logblock[target_lb].cnt++;
            }

            if(bm->check_full(bm,h->logblock[target_lb].plb, MASTER_BLOCK)){ //if block full -> merge call!
                hybrid_merge(h->logblock[target_lb]);
            }
            res = ppn;
        }
    return res;
}

uint32_t hybrid_map_pick(uint32_t lba) {
    hm_body *h=(hm_body*)hybrid_ftl.algo_body;
    blockmanager *bm = hybrid_ftl.bm;
    uint32_t lbn = lba / _PPS;
    uint32_t offset = lba % _PPS;
    uint32_t target_lb = h->datablock[lbn].lb_idx;

    if(bm->is_valid_page(bm,h->logblock[target_lb].lbmapping[offset] )){
        return h->logblock[target_lb].lbmapping[offset];
    }
    return h->datablock[lbn].pba + offset*L2PGAP;
}
