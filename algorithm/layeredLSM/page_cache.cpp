#include "page_cache.h"
#include "../../interface/interface.h"
#include <list>

void pc_set_init(pc_set *target, uint32_t max_cached_size, uint32_t lba_num, lower_info *li){
    lru_init(&target->lru, NULL, NULL);
    target->now_cached_size=0;
    target->max_cached_size=max_cached_size;
    target->pinned_size=0;
    target->cached_sc_num=0;
    target->cached_idx_num=0;

    target->cached_idx.clear();
    target->cached_sc.clear();
    target->cached_sc.reserve(lba_num/RIDXINPAGE+1);
    page_cache temp_pc;
    temp_pc.type=SHORTCUT;
    temp_pc.flag=EMPTY;
    temp_pc.ppa=UINT32_MAX;
    temp_pc.size=PAGESIZE;
    temp_pc.ispinned=false;
    temp_pc.data=NULL;
    temp_pc.node=NULL;

    for(uint32_t i=0; i<lba_num/RIDXINPAGE+1; i++){
        temp_pc.scidx=i;
        target->cached_sc.push_back(temp_pc);
    }

    target->li=li;
}

void pc_set_free(pc_set *target){
    lru_free(target->lru);

    std::map<uint32_t, page_cache*>::iterator miter;
    for(miter=target->cached_idx.begin(); miter!=target->cached_idx.end(); miter++){
        free(miter->second->data);
        delete(miter->second);
    }
    target->cached_sc.clear();
    target->cached_idx.clear();
}

bool pc_is_cached(pc_set *target, cache_type type, uint32_t ppa_or_scidx, bool pinned){
    page_cache *pc=NULL;
    if(type==SHORTCUT){
        pc=&target->cached_sc[ppa_or_scidx];
        if(pc->flag==EMPTY || pc->flag==FLYING){
            pc=NULL;
        }
    }
    else if(type==IDX){
        std::map<uint32_t, page_cache*>::iterator miter;
        miter=target->cached_idx.find(ppa_or_scidx);
        if(miter==target->cached_idx.end()){
            pc=NULL;
        }
        else{
            pc=miter->second;
        }
    }
    else{
        printf("error!\n");
        abort();
    }

    if(pc){
        lru_update(target->lru, pc->node);
        pc->ispinned=pinned;
        if(pinned){
            if(pc->refer_cnt==0){
                target->pinned_size+=pc->size;
            }
           pc->refer_cnt++;
        }
        return true;
    }
    else return false;
}

bool pc_has_space(pc_set *target, uint32_t need_size){
    return target->now_cached_size + need_size < target->max_cached_size;
}

void pc_occupy(pc_set *target, cache_type type, uint32_t ppa_or_scidx, uint32_t size){
    page_cache *pc=NULL;
    if(target->now_cached_size + size > target->max_cached_size){
        printf("size error!\n");
        abort();
    }
    if(type==SHORTCUT){
        pc = &target->cached_sc[ppa_or_scidx];
        pc->flag=EMPTY;
        target->cached_sc_num++;
    }
    else if(type==IDX){
        pc = new page_cache();
        pc->type = type;
        pc->flag = EMPTY;
        pc->size = size;
        pc->ppa=ppa_or_scidx;
        target->cached_idx.insert(std::make_pair(ppa_or_scidx, pc));
        target->cached_idx_num++;
    }
    else{
        printf("error!\n");
        abort();
    }

    pc->flag=OCCUPIED;
    pc->data=malloc(size);
    pc->waiting_req.clear();
    pc->ispinned = true;
    pc->refer_cnt=1;
    pc->node=NULL;

    target->pinned_size+=size;
    target->now_cached_size+=size;
}

void pc_set_insert(pc_set *target, cache_type type, uint32_t ppa_or_scidx, void *data, void (*converter)(void *data, page_cache *pc)){
    page_cache *pc;
    if(type==SHORTCUT){
        pc=&target->cached_sc[ppa_or_scidx];
    }
    else if(type==IDX){
        pc=target->cached_idx[ppa_or_scidx];
        pc->pba=ppa_or_scidx;
        pc->ppa=ppa_or_scidx;
    }
    else{
        printf("error!\n");
        abort();
    }

    if(converter){
        converter(data, pc);
    }
    else{
        memcpy(pc->data, data, pc->size);
    }

    pc->node=lru_push(target->lru, pc);
    pc->waiting_req.clear();
    pc->flag=CLEAN;
}

void pc_set_update(pc_set *target, uint32_t ppa_or_scidx){
    page_cache *pc=&target->cached_sc[ppa_or_scidx];
    /*update data*/
    pc->flag=DIRTY;
}

void pc_unpin(pc_set *target, cache_type type, uint32_t ppa_or_scidx){
    page_cache *pc;
    if(type==SHORTCUT){
        pc=&target->cached_sc[ppa_or_scidx];
    }
    else if(type==IDX){
        pc=target->cached_idx[ppa_or_scidx];
    }
    else{
        printf("error!\n");
        abort();
    }

    if(!pc->ispinned){
        printf("error!\n");
        abort();
    }
    if(pc->ispinned){
        pc->refer_cnt--;
        if (pc->refer_cnt == 0){
            pc->ispinned = false;
            target->pinned_size-= pc->size;
        }
    }

    target->pinned_size-=pc->size;
}

static void *__write_end_req(algo_req *req){
    inf_free_valueset(req->value, FS_MALLOC_W);
    free(req);
    return NULL;
}

static void __send_write_reqeust(pc_set *target, page_cache *pc, uint32_t new_ppa){
    algo_req *res=(algo_req*)calloc(1, sizeof(algo_req));
    res->parents=NULL;
    res->ppa=new_ppa;
    res->type=MAPPINGW;//??
    res->end_req=__write_end_req;
    res->value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
    memcpy(res->value->value, pc->data, PAGESIZE);
    res->value->ppa=new_ppa;
    target->li->write(new_ppa, PAGESIZE, res->value, res);
}

void pc_evict(pc_set *target, bool internal, uint32_t need_size, uint32_t (*get_ppa)(uint32_t sc_idx, page_cache *pc)){
    uint32_t remain_size=target->max_cached_size-target->now_cached_size;
    if(remain_size > need_size) return;
    int32_t target_size=need_size-remain_size;
    std::list<page_cache*> temp_list;
    page_cache *pc;
    while(target_size > 0){
        page_cache *pc=(page_cache*)lru_pop(target->lru);
        if(!pc){
            printf("all cached entries are pinned %s:%d\n", __FUNCTION__, __LINE__);
            abort();
        }

        if(pc->ispinned){
            temp_list.insert(temp_list.end(), pc);
            continue;
        }

        target->now_cached_size-=pc->size;
        target_size-=pc->size;

        if(pc->type==SHORTCUT){
            target->cached_sc_num--;
            if(pc->flag==DIRTY){
                uint32_t new_ppa=get_ppa(pc->scidx, pc);
                __send_write_reqeust(target, pc, new_ppa);
                pc->ppa=new_ppa;
            }
            else if(pc->flag==CLEAN){
                //do nothing
            }
            else{
                printf("invalidate type in cache!\n");
                abort();
            }
            free(pc->data);
            pc->data=NULL;
            pc->node = NULL;
            pc->flag = EMPTY;
        }
        else if(pc->type==IDX){
            target->cached_idx_num--;
            target->cached_idx.erase(pc->ppa);
            free(pc->data);
            delete pc;
        }
        else{
            printf("error!\n");
            abort();
        }
    }

    std::list<page_cache*>::iterator iter;
    for(iter=temp_list.begin(); iter!=temp_list.end(); iter++){
        pc=*iter;
        pc->node=lru_push_last(target->lru, (void*)pc);
    }

    return;
}


void pc_force_evict_idx(pc_set *pcs, uint32_t pba){
    std::map<uint32_t, page_cache*>::iterator miter;
    miter=pcs->cached_idx.find(pba);
    if(miter==pcs->cached_idx.end()){
        return;
    }
    lru_move_last(pcs->lru, miter->second->node);
}

algo_req* pc_send_get_request(pc_set *target, cache_type type, request *parents, uint32_t ppa_or_scidx, value_set *value, void *param, void* (*end_req)(algo_req*)){
    algo_req *res=(algo_req*)calloc(1,sizeof(algo_req));
    res->parents=parents;
    res->type=MAPPINGR;
    res->end_req=end_req;
    res->value = value;
    res->param = (void *)param;

    page_cache *pc=NULL;
    if(type==SHORTCUT){
        pc=&target->cached_sc[ppa_or_scidx];
    }
    else if(type==IDX){
        std::map<uint32_t, page_cache*>::iterator miter;
        miter=target->cached_idx.find(ppa_or_scidx);
        if(miter==target->cached_idx.end()){
            printf("error!\n");
            abort();
        }

        pc=miter->second;
    }
    else{
        printf("error!\n");
        abort();
    }

    if(pc->flag!=OCCUPIED){
        printf("error!\n");
        abort();
    }
    pc->flag=FLYING;
    res->ppa=pc->ppa;

    /*add waiting request*/
    if(pc->waiting_req.size()==0){
        pc->waiting_req.push_back(res);
        target->li->read(res->ppa, PAGESIZE, value, res);
    }
    else{
        pc->waiting_req.push_back(res);
    }

    return res;
}

page_cache *pc_set_pick(pc_set *pcs, cache_type type, uint32_t ppa_or_scidx){
    page_cache *pc=NULL;
    if(type==SHORTCUT){
        pc=&pcs->cached_sc[ppa_or_scidx];
    }
    else if(type==IDX){
        std::map<uint32_t, page_cache*>::iterator miter;
        miter=pcs->cached_idx.find(ppa_or_scidx);
        if(miter==pcs->cached_idx.end()){
            printf("error!\n");
            abort();
        }
        pc=miter->second;
    }
    else{
        printf("error!\n");
        abort();
    }

    return pc;
}