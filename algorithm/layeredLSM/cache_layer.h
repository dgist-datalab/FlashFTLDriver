#pragma once
#include "lsmtree.h"
#include "page_cache.h"
#include "mapping_function.h"
typedef struct cache_read_param{
    bool isinternal;
    bool isdone;
    uint32_t pba_or_scidx;
    cache_type type;
    value_set *value;
    pc_set *pcs;
    page_cache *pc;
    run *r;
    void *parents_req;
}cache_read_param;



void cache_layer_init(lsmtree *lsm, uint32_t cached_size, lower_info *li);
void cache_layer_free(lsmtree *lsm);
void cache_layer_sc_retry(lsmtree *lsm, uint32_t lba, run **ridx, cache_read_param *crp);
void* cache_layer_sc_read(lsmtree *lsm, uint32_t lba, run **ridx, request *parent, bool cache_check, bool *isdone);
void* cache_layer_sc_update(lsmtree *lsm, std::vector<uint32_t> &lba_set, run *des_run, uint32_t size);


void cache_layer_idx_force_evict(lsmtree *lsm, uint32_t pba);
void cache_layer_idx_insert(lsmtree *lsm, uint32_t pba, map_function *mf, bool pinning, bool trivial_move);

void cache_layer_idx_retry(lsmtree *lsm, uint32_t pba, cache_read_param *crp);

void * cache_layer_idx_read(lsmtree *lsm, uint32_t pba, uint32_t lba, run *r, request *req, map_function *mf);
void cache_layer_idx_unpin(lsmtree *lsm, uint32_t pba);
void cache_layer_sc_unpin(lsmtree *lsm, uint32_t lba);

void cache_finalize(cache_read_param *crp);