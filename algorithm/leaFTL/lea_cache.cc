#include "./leaFTL.h"
#include "./page_manager.h"
#include "group.h"
#include "../../include/data_struct/lru_list.h"
#include "./lea_container.h"
#include "./issue_io.h"

extern page_manager *translate_pm;
extern LRU *gp_lru;
extern int64_t lru_max_byte;
extern int64_t lru_now_byte;
extern int64_t lru_reserve_byte;
extern uint32_t max_cached_trans_map;
extern temp_map *gc_translate_map;
extern group main_gp[TRANSMAPNUM];

inline bool check_enough_space(group *gp){
    return gp->size + lru_now_byte < lru_max_byte;
}

void lea_cache_insert(group *gp, uint32_t *piece_ppa){
    /*
    if(!check_enough_space(gp)){
        printf("you must evict before insert!\n");
        abort();
    }*/
        
    if(piece_ppa){
    #ifdef FAST_LOAD_STORE
        group_load_levellist(gp);
    #else
        group_from_translation_map(gp, NULL, piece_ppa, gp->map_idx);
    #endif
        if (lru_reserve_byte < 0){
            //printf("what happend?\n");
            //abort();
            lru_reserve_byte=0;
        }
        else{
            lru_reserve_byte-=gp->size;
        }
    }
    lru_now_byte+=gp->size;
    gp->lru_node=(void*)lru_push(gp_lru, (void*)gp);
    gp->isclean=true;
}

void moved_gp_update(temp_map *map){
    for(uint32_t i=0; i<map->size; i++){
        main_gp[map->lba[i]].ppa=map->piece_ppa[i];
    }
}

void __lea_cache_evict_body(uint32_t size){
    uint32_t getting_space=0;
    uint32_t remain_space;
    while(getting_space<size){
        group *victim=(group*)lru_pop(gp_lru);
        getting_space+=victim->size;

        if(victim->cache_flag!=CACHE_FLAG::CACHED){
            printf("what happend?\n");
            GDB_MAKE_BREAKPOINT;
        }

        if(victim->isclean){
            victim->isclean=true;
            group_store_levellist(victim);
            group_clean(victim, true, true);
            victim->cache_flag=CACHE_FLAG::UNCACHED;
            continue;
        }

        uint32_t *piece_ppa_set=lea_gp_to_mapping(victim); //this includes group store
        if(victim->ppa!=INITIAL_STATE_PADDR){
            //for (uint32_t i = 0; i < L2PGAP; i++){
            invalidate_piece_ppa(translate_pm->bm, victim->ppa * L2PGAP, L2PGAP, true);
            //}
        }
retry:
        remain_space=pm_remain_space(translate_pm, true);
        if(remain_space==0){
            if(pm_assign_new_seg(translate_pm, false)==false){
                pm_gc(translate_pm, gc_translate_map, false);
                if(gc_translate_map->size){
                    moved_gp_update(gc_translate_map);
                    temp_map_clear(gc_translate_map);
                }
            }
            goto retry;
        }
        victim->ppa=pm_map_flush(translate_pm, true, (char*)piece_ppa_set, victim->map_idx);

        victim->isclean=true;
        group_clean(victim, true, true);
        victim->cache_flag=CACHE_FLAG::UNCACHED;
        victim->lru_node=NULL;
    }
    lru_now_byte-=getting_space;
}

void lea_cache_evict_force(){
    if(lru_now_byte < lru_max_byte) return;
    __lea_cache_evict_body(lru_now_byte-lru_max_byte);
}

bool lea_cache_evict(group *gp){ 
    /*not need to evict*/
    if(group_cached(gp)) return true;
    if(gp->size+lru_reserve_byte+lru_now_byte < lru_max_byte) return true;

    /*no space to evict*/
    if(lru_now_byte < gp->size){return false;}

    /*do eviction*/
    __lea_cache_evict_body(gp->size);

    lru_reserve_byte+=gp->size;
    return true;
}

void lea_cache_promote(group *gp){
    //static uint32_t cnt=0;
    //printf("%u promote %u\n", ++cnt, gp->map_idx);
    if(group_cached(gp)==false){
        printf("what happend! %s:%u\n", __FUNCTION__, __LINE__);
        GDB_MAKE_BREAKPOINT;
    }
    lru_update(gp_lru, (lru_node*)gp->lru_node);
}


void lea_cache_size_update(group *gp, uint32_t size, bool decrease){
    if(gp->cache_flag!=CACHE_FLAG::CACHED){
        printf("must be cached!\n");
        GDB_MAKE_BREAKPOINT;
    }

    if(decrease){
        lru_now_byte-=size;
    }
    else{
        lru_now_byte+=size;
    }

    if(lru_now_byte<0){
        //GDB_MAKE_BREAKPOINT;
    }
}