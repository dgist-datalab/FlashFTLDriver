#pragma once
#include "../../include/data_struct/lru_list.h"
#include "../../include/settings.h"
#include "../../include/container.h"
#include <stdint.h>
#include <vector>
#include <map>
#define RIDXINPAGE (PAGESIZE/8*12)

enum cache_type{
    SHORTCUT, IDX
};

enum cache_flag{
    EMPTY, DIRTY, CLEAN, FLYING
};

typedef struct page_cache{
    cache_type type;
    cache_flag flag;
    uint32_t ppa;
    uint32_t scidx;
    uint32_t size;
    bool ispinned;
    void *data;
    lru_node *node;
}page_cache;

typedef struct page_cache_set{
    LRU *lru;
    uint32_t now_cached_size;
    uint32_t max_cached_size;

    uint32_t pinned_size;

    uint32_t cached_sc_num;
    uint32_t cached_idx_num;

    std::vector<page_cache> cached_sc; 
    std::map<uint32_t, page_cache*> cached_idx; //read only cache, key - ppa and data - idx
}pc_set;

//max_cached_page_num at least is equal to QDEPTH
void pc_set_init(pc_set *, uint32_t max_cached_size, uint32_t lba_num);
void pc_set_free(pc_set *);

bool pc_is_cached(pc_set *, cache_type type, uint32_t ppa_or_scidx);
bool pc_has_space(pc_set *);
void pc_occupy(pc_set *, cache_type type, uint32_t ppa_or_scidx, uint32_t size);

void pc_set_insert(pc_set *, cache_type type, uint32_t ppa_or_scidx, void *data, uint32_t size,  bool make_pinned, void (*converter)(void *data, page_cache *pc));
void pc_set_update(pc_set *, uint32_t ppa_or_scidx);
void pc_unpin(pc_set *, cache_type type, uint32_t ppa_or_scidx);
void pc_evict(pc_set *, bool internal);

/*Request interface*/
/*1. update sc while the data resides in memtable*/
/*2. udpate sc while compaction*/
/*3. read request for sc*/
/*4. read request for idx*/

