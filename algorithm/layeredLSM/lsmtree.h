#ifndef LSMTREE
#define LSMTREE
#include "./level.h"
#include "./block_table.h"
#include "./run.h"
#include "./shortcut.h"
#include "../../include/container.h"
#include "../../include/utils/thpool.h"
#include "./page_cache.h"
#include <queue>
#include <stdlib.h>
#define MEMTABLE_NUM 1
//#define THREAD_COMPACTION


enum{
	READ_REQ_INIT, READ_REQ_SC, READ_REQ_MAP, READ_REQ_DATA, READ_REQ_DONE,
};

typedef struct page_read_buffer{
	std::multimap<uint32_t, algo_req *> * pending_req;
	std::multimap<uint32_t, algo_req *>* issue_req;
	fdriver_lock_t pending_lock;
	fdriver_lock_t read_buffer_lock;
	uint32_t buffer_ppa;
	char buffer_value[PAGESIZE];
}page_read_buffer;

typedef struct range{
	uint32_t start;
	uint32_t end;
}range;

typedef struct run_manager{
	uint32_t total_run_num;
	std::queue<uint32_t>* ridx_queue;
	run **run_array;
	fdriver_lock_t lock;
}run_manager;

typedef struct lsmtree_parameter{
	bool cache_flag;
	float fpr;
	uint32_t memtable_entry_num;
	uint32_t target_bit;
	double size_factor;
	uint32_t total_level_num;
	uint32_t total_run_num;
	int32_t spare_run_num;
	uint64_t shortcut_bit;

	uint64_t BF_bit;
	uint64_t PLR_bit;
	uint64_t L0_bit;

	double per_bf_bit;
	double per_plr_bit;

	range BF_level_range;
	range PLR_level_range;
	uint64_t max_memory_usage_bit;
	uint32_t memory_limit;

	bool sc_pinning;
}lsmtree_parameter;

typedef struct lsmtree_monitor{
	uint64_t now_mapping_memory_usage;
	uint64_t max_mapping_memory_usage;

	uint64_t plr_memory_ent;
	uint64_t plr_memory_usage;
	uint64_t bf_memory_ent;
	uint64_t bf_memory_usage;

	uint32_t compaction_cnt[10];
	uint64_t compaction_input_entry_num[10];
	uint64_t compaction_output_entry_num[10];
	uint32_t force_compaction_cnt;
	uint32_t reinsert_cnt;

	uint64_t memtable_search_cnt;
	uint64_t memtable_search_time;

	uint64_t binary_search_cnt[2];
	uint64_t binary_search_time[2];

	uint64_t bf_cnt;
	uint64_t bf_time;
	uint64_t bf_retry_cnt;
	uint64_t bf_retry_time;

	uint64_t plr_cnt;
	uint64_t plr_time;
	uint64_t plr_retry_cnt;
	uint64_t plr_retry_time;

	uint64_t issue_cnt;
	uint64_t issue_time;

	uint64_t read_total_time;
	uint64_t read_total_cnt;

	uint64_t map_search_time;
	uint64_t map_search_cnt;

	uint64_t sorting_time;
	uint64_t sorting_cnt;

	uint64_t compaction_io_time;
	uint64_t compaction_io_cnt;

	uint64_t compaction_io_entry_num;

	uint64_t total_compaction_time;
	uint64_t total_compaction_cnt;
}lsmtree_monitor;

typedef struct lsmtree{
	run_manager *rm;
	blockmanager* master_sm;
	struct shortcut_master *shortcut;
	pc_set *pcs;
	L2P_bm *bm;
	uint32_t now_memtable_idx;
	run *memtable[MEMTABLE_NUM];
	struct level **disk;
	threadpool tp;
	lsmtree_monitor monitor;
	lsmtree_parameter param;
	fdriver_lock_t lock;

	fdriver_lock_t read_cnt_lock;
	uint32_t now_flying_read_cnt;
	__segment *sc_seg;
}lsmtree;

/*
 * Function:lsmtree_calculate_parameter
 * --------------------
 *		return lsmtree_parameter
 *
 * fpr: target fpr
 * memory_usage:max memory usage
 * LBA_range: the coverage 
 * */
lsmtree_parameter lsmtree_calculate_parameter(float fpr, uint32_t target_bit, uint64_t memory_usage, uint64_t max_LBA_num, bool cache_flag);
uint32_t lsmtree_argument_set(int argc, char **argv);

void lsmtree_print_param(lsmtree_parameter param);

/*
 * Function:lsmtree_init
 * --------------------
 *		return new lsmtree by using param
 *
 * param parameter for lsmtree
 *
 * */
lsmtree* lsmtree_init(lsmtree_parameter param, blockmanager *sm);

void lsmtree_free(lsmtree *lsm);

uint32_t lsmtree_insert(lsmtree *lsm, request *req);
uint32_t lsmtree_read(lsmtree *lsm, request *req);
uint32_t lsmtree_print_log(lsmtree *lsm);
void lsmtree_run_print(lsmtree* lsm);

uint32_t lsmtree_dump(lsmtree *lsm, FILE *fp);
lsmtree* lsmtree_load(FILE *fp, blockmanager *sm);

struct run *__lsm_populate_new_run(lsmtree *lsm, uint32_t map_type, uint32_t run_type, uint32_t entry_num, uint32_t level_num);
void __lsm_free_run(lsmtree *lsm, run *r);
void __lsm_calculate_memory_usage(lsmtree *lsm,uint64_t entry_num, int32_t memory_usage_bit, uint32_t map_type, bool pinning);
bool __lsm_pinning_enable(lsmtree *lsm, uint32_t entry_num);
uint32_t lsm_get_ppa_from_scseg(void *arg, void (*update_addr)(uint32_t *, uint32_t*, uint32_t));

void time_breakdown_start();
uint64_t time_breakdown_end();

static inline uint32_t bit_calculate(uint32_t lba_range){
	uint32_t res=0;
	while(lba_range>(1<<res)){
		res++;
	}
	return res;
}

#endif
