#include "cache_layer.h"
#include <cmath>
#include <algorithm>

static char __temp_cache_data[PAGESIZE];
static lsmtree *main_lsm;
void cache_layer_init(lsmtree *lsm, uint32_t cached_size, lower_info *li){
    lsm->pcs=new pc_set();
    pc_set_init(lsm->pcs, cached_size, RANGE, li);
    main_lsm=lsm;
}

void cache_layer_free(lsmtree *lsm){
    pc_set_free(lsm->pcs);
    delete lsm->pcs;
}

void cache_update_pc_ppa_by_gc(uint32_t *scidx_set, uint32_t *new_ppa_set, uint32_t size){
    static int cnt=0;
    printf("--------------------%d\n",cnt);
    for(uint32_t i=0; i<size; i++){
        page_cache *target_pc=pc_set_pick(main_lsm->pcs, SHORTCUT, scidx_set[i]);
        if(target_pc->flag==DIRTY){
            target_pc->flag=CLEAN;
        }
        target_pc->ppa=new_ppa_set[i];
    }
}

uint32_t cache_get_ppa(uint32_t cache_idx, page_cache *pc){
    uint32_t old_ppa=pc->ppa;
    uint32_t res=lsm_get_ppa_from_scseg(main_lsm, cache_update_pc_ppa_by_gc);
    if(old_ppa!=UINT32_MAX){
        main_lsm->master_sm->bit_unset(main_lsm->master_sm, old_ppa*L2PGAP);
    }
    main_lsm->master_sm->set_oob(main_lsm->master_sm, (char*)&pc->scidx, sizeof(uint32_t), res);
    main_lsm->master_sm->bit_set(main_lsm->master_sm, res*L2PGAP);
    return res;
}

void *cache_layer_read_endreq(algo_req* req){
    cache_read_param *param=(cache_read_param*)req->param;

    page_cache *target_pc=pc_set_pick(param->pcs, SHORTCUT, param->pba_or_scidx);
    
    std::vector<algo_req*>::iterator iter;
    for(iter=target_pc->waiting_req.begin(); iter!=target_pc->waiting_req.end(); iter++){
        algo_req *temp_req=*iter;
        param=(cache_read_param*)(*iter)->param;
        if(param->isinternal){
            param->isdone=true;
        }
        else{
            param->isdone=true;
            std::vector<algo_req*>::iterator iter;
            inf_assign_try(req->parents);
        }
        free(temp_req);
    }
    return NULL;
}

void cache_layer_sc_retry(lsmtree *lsm, uint32_t lba, run **ridx, cache_read_param *crp){
    pc_set_insert(lsm->pcs, SHORTCUT, lba/RIDXINPAGE, (void*)__temp_cache_data, NULL);
    (*ridx)=shortcut_query(lsm->shortcut, lba);
    cache_finalize(crp);
    pc_unpin(lsm->pcs, SHORTCUT, lba/RIDXINPAGE);
}

static inline void __sc_udpate(lsmtree *lsm, std::vector<uint32_t> &lba_set, run* des_run, 
uint32_t size, 
std::list<std::pair<uint32_t, uint32_t> >::iterator iter, 
std::list<std::pair<uint32_t, uint32_t> >::iterator end_iter){
    std::list<std::pair<uint32_t, uint32_t> >::iterator nxt_iter;
    nxt_iter=++iter;
    iter--;

    pc_set_update(lsm->pcs, (*iter).first);
    uint32_t current=(*iter).second;

    uint32_t last;
    if(nxt_iter==end_iter){
        last=size;
    }
    else{
        last=(*nxt_iter).second;
    }

    std::vector<uint32_t> lba_target;
    lba_target.assign(lba_set.begin()+current, lba_set.begin()+last);
    shortcut_link_bulk_lba(lsm->shortcut, des_run, &lba_target, true);
}

void* cache_layer_sc_read(lsmtree *lsm, uint32_t lba, run **ridx, request *parent, bool cache_check, bool *isdone){
    //if parent==NULL --> internal request
    uint32_t sc_idx=lba/RIDXINPAGE;
    (*isdone)=true;
    (*ridx)=NULL;
    if(cache_check && pc_is_cached(lsm->pcs, SHORTCUT, sc_idx, false)){
        (*ridx)=shortcut_query(lsm->shortcut, lba);
        return NULL;
    }
    else{
        page_cache *target_pc=pc_set_pick(lsm->pcs, SHORTCUT, sc_idx);
        if(parent && target_pc->ppa==UINT32_MAX){ //read not found case
            (*ridx)=NULL;
            return NULL;
        }

        /*
        static uint32_t cnt=0;
        printf("read cnt:%u\n", ++cnt);
        if(cnt==172){
            printf("here\n");
        }
        */

        uint32_t target_size=PAGESIZE;
        if(pc_has_space(lsm->pcs, target_size)){
            /*do nothing*/           
        }
        else{
            /*do eviction*/
            if(lsm->pcs->now_cached_size-lsm->pcs->pinned_size < target_size){
                //too many  requests
                (*isdone)=false;
                return NULL;
            }
            pc_evict(lsm->pcs, true, target_size, cache_get_ppa);
        }

        pc_occupy(lsm->pcs, SHORTCUT, sc_idx, target_size);
        if(target_pc->ppa==UINT32_MAX){
            /*initial state*/
            pc_set_insert(lsm->pcs, SHORTCUT, sc_idx, (void *)__temp_cache_data, NULL);
            (*ridx) = NULL;
            return NULL;
        }

        cache_read_param *rparam = (cache_read_param *)malloc(sizeof(cache_read_param));
        rparam->isdone=false;
        value_set *value=NULL;
        if(parent){
            value=parent->value; 
            rparam->isinternal = false;
        } 
        else {
            rparam->isinternal = true;
            value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
        }
        rparam->value=value;
        rparam->pcs=lsm->pcs;
        rparam->pba_or_scidx=sc_idx;

        if(parent){
            parent->param=(void*)rparam;
            parent->retry=true;
        }

        pc_send_get_request(lsm->pcs, SHORTCUT, parent, sc_idx, value, rparam, cache_layer_read_endreq);
        return (void*)rparam;
    }
}

void* cache_layer_sc_update(lsmtree *lsm, std::vector<uint32_t> &lba_set, run *des_run, uint32_t size){
    //figure out which sc_idx to update
    std::list<std::pair<uint32_t, uint32_t> > target_sc_array; //first-->scidx, second-->start idx
    uint32_t previous_sc_idx=UINT32_MAX;
    for(uint32_t i=0; i<size; i++){
        if(lba_set[i]/RIDXINPAGE!=previous_sc_idx){
            target_sc_array.push_back(std::pair<uint32_t, uint32_t>(lba_set[i]/RIDXINPAGE,i));
            previous_sc_idx=lba_set[i]/RIDXINPAGE;
        }
    }

    //1. check the sc_page is cached or not
    //if the sc_page is cached, then update the sc_page
    //and find not cached sc_page
    std::list<std::pair<uint32_t, uint32_t> >::iterator iter;
    for(iter=target_sc_array.begin(); iter!=target_sc_array.end();){
        if(pc_is_cached(lsm->pcs, SHORTCUT, (*iter).first, false)){
            //get next iteration

            __sc_udpate(lsm, lba_set, des_run, size, iter, target_sc_array.end());

            target_sc_array.erase(iter++);
        }
        else{
            iter++;
        }
    }

    //2. send read request for not cached sc_page and update
    typedef std::pair<cache_read_param*, std::list<std::pair<uint32_t, uint32_t> >::iterator> temp_pair;
    iter=target_sc_array.begin();
    while(iter!=target_sc_array.end()){
        std::list<temp_pair > crp_list;
        //send request
        for(; iter!=target_sc_array.end(); iter++){
            run *temp_ridx;
            bool isdone;
            cache_read_param *crp=(cache_read_param*)cache_layer_sc_read(lsm, (*iter).first*RIDXINPAGE, &temp_ridx, NULL, false, &isdone);

            if(crp==NULL){//initial state
                if(isdone==false){ 
                    /*too many flying request*/
                    break;
                }
                __sc_udpate(lsm, lba_set, des_run, size, iter, target_sc_array.end());
                pc_unpin(lsm->pcs, SHORTCUT, iter->first);
                continue;
            }
            else{
                crp_list.push_back(temp_pair(crp, iter));
            }
        }

        //update request
        std::list<temp_pair>::iterator crp_iter;
        while(crp_list.size()!=0){
            for(crp_iter=crp_list.begin(); crp_iter!=crp_list.end(); ){
                cache_read_param *crp=(*crp_iter).first;
                if(crp->isdone==false){
                    crp_iter++;
                    continue;
                }
                pc_set_insert(lsm->pcs, SHORTCUT, (*(*crp_iter).second).first, (void*)__temp_cache_data, NULL);

                __sc_udpate(lsm, lba_set, des_run, size, (*crp_iter).second, target_sc_array.end());
                
                pc_unpin(lsm->pcs, SHORTCUT, (*(*crp_iter).second).first);
                cache_finalize(crp);
                crp_list.erase(crp_iter++);
            }
        }
    }
    return NULL;
}

bool __cache_check_and_occupy(lsmtree *lsm, uint32_t pba, map_function *mf, bool read_path){
    if(read_path && pc_is_cached(lsm->pcs, IDX, pba, true)){
        return true;
    }
    else{
        uint32_t target_size=mf->get_memory_usage(mf, 32)/8;
        if(pc_has_space(lsm->pcs, target_size)){
            /*do nothing*/           
        }
        else{
            /*do eviction*/
            pc_evict(lsm->pcs, true, target_size, cache_get_ppa);
        }

        pc_occupy(lsm->pcs, IDX, pba, target_size);
    }
    return false;
}

void cache_layer_idx_insert(lsmtree *lsm, uint32_t pba, map_function *mf, bool pinning, bool trivial_move){
    if(__cache_check_and_occupy(lsm, pba, mf, pinning | trivial_move)){
        //hit case
        return;
    }

    //miss case
    pc_set_insert(lsm->pcs, IDX, pba, (void*)__temp_cache_data, NULL);
    if(!pinning){
        pc_unpin(lsm->pcs, IDX, pba);
    }
}

void cache_layer_idx_force_evict(lsmtree *lsm, uint32_t pba){
    pc_force_evict_idx(lsm->pcs, pba);
}

void *cache_layer_read_idx_endreq(algo_req *req){
    cache_read_param *param=(cache_read_param*)req->param;
    page_cache *target_pc=pc_set_pick(param->pcs, IDX, param->pba_or_scidx);

    std::vector<algo_req*>::iterator iter;
    for(iter=target_pc->waiting_req.begin(); iter!=target_pc->waiting_req.end(); iter++){
        algo_req *temp_req=*iter;
        param=(cache_read_param*)(*iter)->param;
        if(param->isinternal){
            param->isdone=true;
        }
        else{
            param->isdone=true;
            std::vector<algo_req*>::iterator iter;
            inf_assign_try(req->parents);
        }
        free(temp_req);
    }    
    return NULL;
}

void *cache_layer_idx_read(lsmtree *lsm, uint32_t pba, uint32_t lba, run *r, request *parent, map_function *mf){
    if(__cache_check_and_occupy(lsm, pba, mf, true)){
        /*hit case*/
        return NULL;
    }

    //printf("pba %u\n",pba);
    /*miss case*/
    cache_read_param *rparam = (cache_read_param *)malloc(sizeof(cache_read_param));
    rparam->isdone=false;
    value_set *value=NULL;
    if(parent){
        value=parent->value; 
        rparam->isinternal = false;
    } 
    else {
        printf("parent request is none");
        abort();
    }

    rparam->value=value;
    rparam->pcs=lsm->pcs;
    rparam->pba_or_scidx=pba;
    rparam->r=r;
    rparam->parents_req=(void*)parent;
    rparam->isinternal=false;

    parent->param=(void*)rparam;
    parent->retry=true;

    pc_send_get_request(lsm->pcs, IDX, parent, pba, value, rparam, cache_layer_read_idx_endreq);

    return (void*)rparam;
}

void cache_layer_idx_retry(lsmtree *lsm, uint32_t pba, cache_read_param *crp){
    pc_set_insert(lsm->pcs, IDX, pba, (void*)__temp_cache_data, NULL);   
    cache_finalize(crp);
}

void cache_finalize(cache_read_param *crp){
    if(crp->isinternal){
        inf_free_valueset(crp->value, FS_MALLOC_R);
    }
    free(crp);
}


void cache_layer_idx_unpin(lsmtree *lsm, uint32_t pba){
    pc_unpin(lsm->pcs, IDX, pba);
}