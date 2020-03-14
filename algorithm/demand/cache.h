/*
 * Header for Cache module
 */

#ifndef __DEMAND_CACHE_H__
#define __DEMAND_CACHE_H__

#include "../../include/data_struct/lru_list.h"
#include "demand.h"

/* Structures */
struct cache_env {
	cache_t c_type;

	int nr_tpages_optimal_caching;
	int nr_valid_tpages;
	int nr_valid_tentries;

	float caching_ratio;
	int max_cached_tpages;
	int max_cached_tentries;

	/* add attributes here */
};

struct cache_member {
	struct cmt_struct **cmt;
	struct pt_struct **mem_table;
	LRU *lru;

	int nr_cached_tpages;
	int nr_cached_tentries;

	/* add attributes here */
};

struct cache_stat {
	/* cache performance */
	uint64_t cache_hit;
	uint64_t cache_miss;
	uint64_t clean_evict;
	uint64_t dirty_evict;
	uint64_t blocked_miss;

	/* add attributes here */
};


struct demand_cache {
	int (*create) (cache_t c_type, struct demand_cache *);
	int (*destroy) ();

	int (*load) (lpa_t lpa, request *const req, snode *wb_entry);
	int (*list_up) (lpa_t lpa, request *const req, snode *wb_entry);
	int (*wait_if_flying) (lpa_t lpa, request *const req, snode *wb_entry);

	int (*touch) (lpa_t lpa);
	int (*update) (lpa_t lpa, struct pt_struct pte);

	struct pt_struct (*get_pte) (lpa_t lpa);
	struct cmt_struct *(*get_cmt) (lpa_t lpa);

	bool (*is_hit) (lpa_t lpa);
	bool (*is_full) ();

	struct cache_env env;
	struct cache_member member;
	struct cache_stat stat;
};

/* Functions */
struct demand_cache *select_cache(cache_t type);
void print_cache_stat(struct cache_stat *_stat);

#endif
