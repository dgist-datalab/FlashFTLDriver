#include "page_cache.h"

void pc_set_init(pc_set *target, uint32_t max_cached_size, uint32_t lba_num){
    target->lru=(LRU*)malloc(sizeof(LRU));
    lru_init(&target->lru, NULL, NULL);
    target->now_cached_size=0;
    target->max_cached_size=max_cached_size;
    target->pinned_size=0;
    target->cached_sc_num=0;
    target->cached_idx_num=0;

    target->cached_sc.reserve(lba_num/RIDXINPAGE+1);
    std::vector<page_cache>::iterator iter;
    uint32_t sc_idx=0;
    for(iter=target->cached_sc.begin(); iter!=target->cached_sc.end(); iter++, sc_idx++){
        iter->type=SHORTCUT;
        iter->flag=EMPTY;
        iter->ppa=UINT32_MAX;
        iter->scidx=sc_idx;
        iter->size=RIDXINPAGE;
        iter->ispinned=false;
        iter->data=NULL;
        iter->node=NULL;
    }

    target->cached_idx.clear();
}

void pc_set_free(pc_set *target){
    lru_free(target->lru);
    free(target->lru);

    std::map<uint32_t, page_cache*>::iterator miter;
    for(miter=target->cached_idx.begin(); miter!=target->cached_idx.end(); miter++){
        free(miter->second->data);
        free(miter->second);
    }
    target->cached_sc.clear();
    target->cached_idx.clear();
}

bool pc_is_cached(pc_set *target, cache_type type, uint32_t ppa_or_scidx){
    if(type==SHORTCUT){
        return target->cached_sc[ppa_or_scidx].flag!=EMPTY;
    }
    else if(type==IDX){
        return target->cached_idx.find(ppa_or_scidx)!=target->cached_idx.end();
    }
    else{
        printf("error!\n");
        abort();
    }
}

bool pc_has_space(pc_set *target){
    return target->now_cached_size < target->max_cached_size;
}

void pc_occupy(pc_set *target, cache_type type, uint32_t ppa_or_scidx, uint32_t size){
    page_cache *pc=NULL;
    if(type==SHORTCUT){
        pc = &target->cached_sc[ppa_or_scidx];
        pc->flag=EMPTY;
        target->cached_sc_num++;
    }
    else if(type==IDX){
        pc = (page_cache *)malloc(sizeof(page_cache));
        pc->type = type;
        pc->flag = EMPTY;
        pc->size = size;
        pc->data = malloc(size);
        pc->ispinned = true;
        pc->ppa=ppa_or_scidx;
        target->cached_idx.insert(std::make_pair(ppa_or_scidx, pc));
        target->cached_idx_num++;
    }
    else{
        printf("error!\n");
        abort();
    }

    target->pinned_size+=size;
    target->now_cached_size+=size;
}

void pc_set_insert(pc_set *target, cache_type type, uint32_t ppa_or_scidx, void *data, uint32_t size, void (*converter)(void *data, page_cache *pc)){
    if(target->now_cached_size + size > target->max_cached_size){
        printf("size error!\n");
        abort();
    }

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

    if(converter){
        converter(data, pc);
    }

    pc->node=lru_push(target->lru, pc);
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
    pc->ispinned=false;
    target->pinned_size-=pc->size;
}

void pc_evict(pc_set *target, bool internal){
    page_cache *pc=(page_cache*)lru_pop(target->lru);
    if(pc->ispinned){
        printf("error!\n");
        abort();
    }

    pc->
}