#ifndef LSMTREE
#define LSMTREE
#include "./level.h"
#include "./block_table.h"
#include "./run.h"
#include "./shortcut.h"
#include "../../include/container.h"
#include <queue>
#include <stdlib.h>
#define MEMTABLE_NUM 2

typedef struct range{
	uint32_t start;
	uint32_t end;
}range;

typedef struct run_manager{
	uint32_t total_run_num;
	std::queue<uint32_t>* ridx_queue;
	run **run_array;
}run_manager;

typedef struct lsmtree_parameter{
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
	range BF_level_range;
	range PLR_level_range;
	uint64_t max_memory_usage_bit;
}lsmtree_parameter;

typedef struct lsmtree_monitor{
	uint64_t now_memory_usage;
	uint32_t compaction_cnt;
}lsmtree_monitor;

typedef struct lsmtree{
	run_manager *rm;
	struct shortcut_master *shortcut;
	L2P_bm *bm;
	uint32_t now_memtable_idx;
	run *memtable[MEMTABLE_NUM];
	struct level **disk;

	lsmtree_monitor monitor;
	lsmtree_parameter param;
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
lsmtree_parameter lsmtree_calculate_parameter(float fpr, uint32_t target_bit, uint64_t memory_usage, uint64_t max_LBA_num);
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

struct run *__lsm_populate_new_run(lsmtree *lsm, uint32_t map_type, uint32_t run_type, uint32_t entry_num);
void __lsm_free_run(lsmtree *lsm, run *r);
void __lsm_calculate_memory_usage(lsmtree *lsm, int32_t memory_usage_bit);
bool __lsm_pinning_enable(lsmtree *lsm, uint32_t entry_num);

static inline uint32_t bit_calculate(uint32_t lba_range){
	uint32_t res=0;
	while(lba_range>(1<<res)){
		res++;
	}
	return res;
}

#endif
