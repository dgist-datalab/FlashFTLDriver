#pragma once
#include "../../include/data_struct/lru_list.h"
#include "../../include/settings.h"
#include "../../include/container.h"
#include <stdint.h>
#include <vector>
#include <map>
#define RIDXINPAGE (PAGESIZE*8/5)

enum cache_type{
    SHORTCUT, IDX
};

enum cache_flag{
    EMPTY, OCCUPIED, DIRTY, CLEAN, FLYING
};

typedef struct page_cache{
    cache_type type;
    cache_flag flag;
    uint32_t ppa;
    uint32_t pba;
    uint32_t scidx;
    uint32_t size;
    //uint32_t refer_cnt;
    //bool ispinned;
    char *data;
    lru_node *node;
    std::vector<algo_req*> waiting_req;
}page_cache;

typedef struct page_cache_set{
    LRU *lru;
    int32_t now_cached_size;
    int32_t max_cached_size;

    //uint32_t pinned_size;

    uint32_t cached_sc_num;
    uint32_t cached_idx_num;

    std::vector<page_cache> cached_sc; 
    std::map<uint32_t, page_cache*> cached_idx; //read only cache, key - ppa and data - idx
    std::map<uint32_t, std::vector<algo_req*>* > pending_req_map;
    std::map<uint32_t, std::vector<algo_req*>* > pending_sc_req_map;
    lower_info *li;
}pc_set;

//max_cached_page_num at least is equal to QDEPTH
void pc_set_init(pc_set *, uint32_t max_cached_size, uint32_t lba_num, lower_info *li);
void pc_set_free(pc_set *);

page_cache* pc_is_cached(pc_set *, cache_type type, uint32_t ppa_or_scidx, bool pinned);
bool pc_has_space(pc_set *, uint32_t need_size);
page_cache* pc_occupy(pc_set *, cache_type type, uint32_t ppa_or_scidx, uint32_t size);
void pc_reclaim(pc_set *pcs, page_cache *pc);
void pc_set_insert(pc_set *, cache_type type, uint32_t ppa_or_scidx, void *data, void (*converter)(void *data, page_cache *pc), uint32_t (*get_ppa)(uint32_t sc_idx, page_cache*));
void pc_set_update(pc_set *, uint32_t ppa_or_scidx);
//void pc_unpin(pc_set *, cache_type type, uint32_t ppa_or_scidx);
//void pc_unpin_target(pc_set *, page_cache *pc);
void pc_evict(pc_set *, bool internal, int32_t need_size, uint32_t (*get_ppa)(uint32_t sc_idx, page_cache *));
void pc_force_evict_idx(pc_set *pcs, uint32_t pba);

algo_req* pc_send_get_request(pc_set *, cache_type type, request *parents, uint32_t ppa_or_scidx, value_set* value,void *param, void*(*end_req)(algo_req*));
page_cache *pc_set_pick(pc_set *, cache_type type, uint32_t ppa_or_scidx, bool lock_flag);
//void pc_increase_refer_cnt(pc_set *pcs, page_cache *pc);

/*Request interface*/
/*1. update sc while the data resides in memtable*/
/*2. udpate sc while compaction*/
/*3. read request for sc*/
/*4. read request for idx*/

