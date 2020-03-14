/*
 * Demand-based FTL Main Header
 */

#ifndef __DEMAND_H__
#define __DEMAND_H__

#include <stdint.h>
#include <pthread.h>
#include "d_type.h"
#include "d_param.h"
#include "d_htable.h"
#include "../../interface/queue.h"
#include "../../include/container.h"
#include "../../include/dl_sync.h"
#include "../Lsmtree/skiplist.h"
#include "../../include/data_struct/lru_list.h"
#include "../../include/data_struct/redblack.h"


/* Structures */
// Page table entry
struct pt_struct {
	ppa_t ppa; // Index = lpa
#ifdef STORE_KEY_FP
	fp_t key_fp;
#endif
};

// Cached mapping table
struct cmt_struct {
	int32_t idx;
	struct pt_struct *pt;
	NODE *lru_ptr;
	ppa_t t_ppa;

	cmt_state_t state;
	bool is_flying;

	queue *retry_q;
	queue *wait_q;

	bool *is_cached;
	uint32_t cached_cnt;
	uint32_t dirty_cnt;
};

struct hash_params {
	uint32_t hash;
#ifdef STORE_KEY_FP
	fp_t key_fp;
#endif
	int cnt;
	int find;
	uint32_t lpa;

#ifdef DVALUE
	int fl_idx;
#endif
};

struct demand_params{
	value_set *value;
	snode *wb_entry;
	//struct cmt_struct *cmt;
	dl_sync *sync_mutex;
	int offset;
};

struct inflight_params{
	jump_t jump;
	//struct pt_struct pte;
};

struct flush_node {
	ppa_t ppa;
	value_set *value;
};

struct flush_list {
	int size;
	struct flush_node *list;
};


/* Wrapper structures */
struct demand_env {
	int nr_pages;
	int nr_blocks;
	int nr_segments;

	int nr_tsegments;
	int nr_tpages;
	int nr_dsegments;
	int nr_dpages;

	volatile uint64_t wb_flush_size;

#if defined(HASH_KVSSD) && defined(DVALUE)
	int nr_grains;
	int nr_dgrains;
#endif

	int cache_id;
	float caching_ratio;
};

struct demand_member {
	pthread_mutex_t op_lock;

	LRU *lru;
	skiplist *write_buffer;
	snode **sorted_list;

	queue *flying_q;
	queue *blocked_q;
	queue *wb_master_q;
	queue *wb_retry_q;

	queue *range_q;

	struct flush_list *flush_list;

	volatile int nr_valid_read_done;
	volatile int nr_tpages_read_done;

	struct d_htable *hash_table;

#ifdef HASH_KVSSD
	int max_try;
#endif
};

struct demand_stat {
	/* device traffic */
	uint64_t data_r;
	uint64_t data_w;
	uint64_t trans_r;
	uint64_t trans_w;
	uint64_t data_r_dgc;
	uint64_t data_w_dgc;
	uint64_t trans_r_dgc;
	uint64_t trans_w_dgc;
	uint64_t trans_r_tgc;
	uint64_t trans_w_tgc;

	/* gc trigger count */
	uint64_t dgc_cnt;
	uint64_t tgc_cnt;
	uint64_t tgc_by_read;
	uint64_t tgc_by_write;

	/* r/w specific traffic */
	uint64_t read_req_cnt;
	uint64_t write_req_cnt;

	uint64_t d_read_on_read;
	uint64_t d_write_on_read;
	uint64_t t_read_on_read;
	uint64_t t_write_on_read;
	uint64_t d_read_on_write;
	uint64_t d_write_on_write;
	uint64_t t_read_on_write;
	uint64_t t_write_on_write;

	/* write buffer */
	uint64_t wb_hit;

#ifdef HASH_KVSSD
	uint64_t w_hash_collision_cnt[MAX_HASH_COLLISION];
	uint64_t r_hash_collision_cnt[MAX_HASH_COLLISION];

	uint64_t fp_match_r;
	uint64_t fp_match_w;
	uint64_t fp_collision_r;
	uint64_t fp_collision_w;
#endif

};

/* Functions */
uint32_t demand_argument_set(int argc, char **argv);
uint32_t demand_create(lower_info*, blockmanager*, algorithm*);
void demand_destroy(lower_info*, algorithm*);
uint32_t demand_read(request *const);
uint32_t demand_write(request *const);
uint32_t demand_remove(request *const);

uint32_t __demand_read(request *const);
uint32_t __demand_write(request *const);
uint32_t __demand_remove(request *const);
void *demand_end_req(algo_req*);

int range_create();
uint32_t demand_range_query(request *const);
bool range_end_req(request *);

#ifdef DVALUE
int grain_create();
int is_valid_grain(pga_t);
int contains_valid_grain(blockmanager *, ppa_t);
int validate_grain(blockmanager *, pga_t);
int invalidate_grain(blockmanager *, pga_t);
#endif

#endif
