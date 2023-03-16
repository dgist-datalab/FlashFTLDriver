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
    return gp->size + lru_now_byte > lru_max_byte;
}

void lea_cache_insert(group *gp, uint32_t *piece_ppa){
    if(!check_enough_space(gp)){
        printf("you must evict before insert!\n");
        abort();
    }

    group_from_translation_map(gp, NULL, piece_ppa, gp->map_idx);
    gp->lru_node=(void*)lru_push(gp_lru, (void*)gp);
    if(lru_reserve_byte < 0){
        printf("what happend?\n");
        abort();
    }
    gp->isclean=true;
    lru_reserve_byte-=gp->size;
}

void moved_gp_update(temp_map *map){
    for(uint32_t i=0; i<map->size; i++){
        main_gp[map->lba[i]].ppa=map->piece_ppa[i];
    }
}

bool lea_cache_evict(group *gp){ 
    /*not need to evict*/
    if(group_cached(gp)) return true;
    if(gp->size+lru_reserve_byte+lru_now_byte < lru_max_byte) return true;

    /*no space to evict*/
    if(lru_now_byte < gp->size){return false;}

    /*do eviction*/
    uint32_t getting_space=0;
    uint32_t remain_space;
    while(getting_space<gp->size){
        group *victim=(group*)lru_pop(gp_lru);
        getting_space+=gp->size;

        if(victim->cache_flag!=CACHE_FLAG::CACHED){
            printf("what happend?\n");
            GDB_MAKE_BREAKPOINT;
        }

        if(victim->isclean){
            victim->isclean=true;
            group_clean(victim, true);
            continue;
        }

        uint32_t *piece_ppa_set=lea_gp_to_mapping(victim);
        if(victim->ppa!=INITIAL_STATE_PADDR){
            for (uint32_t i = 0; i < L2PGAP; i++){
                invalidate_piece_ppa(translate_pm->bm, victim->ppa * L2PGAP + i);
            }
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
        group_clean(victim, true);
    }

    lru_reserve_byte+=gp->size;
    return true;
}

void lea_cache_promote(group *gp){
    if(group_cached(gp)==false){
        printf("what happend! %s:%u\n", __FUNCTION__, __LINE__);
        GDB_MAKE_BREAKPOINT;
    }
    lru_update(gp_lru, (lru_node*)gp->lru_node);
}