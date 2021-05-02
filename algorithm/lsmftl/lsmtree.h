#ifndef __LSMTREE_H__
#define __LSMTREE_H__

#include "../../include/sem_lock.h"
#include "../../include/container.h"
#include "../../include/utils/rwlock.h"
#include "page_manager.h"
#include "write_buffer.h"
#include "version.h"
#include "level.h"
#include "compaction.h"
#include "./design_knob/lsmtree_param_module.h"
#include "read_helper.h"
#include "version.h"
#include "helper_algorithm/bf_set.h"
#include "segment_level_manager.h"
#include "../../bench/measurement.h"
#include <deque>

#define TARGETFPR 0.1f
#define COMPACTION_REQ_MAX_NUM 1
#define WRITEBUFFER_NUM (1+1)
#define TIMERESULT

typedef struct lsmtree_monitor{
	/*cnt*/
	uint32_t trivial_move_cnt;
	uint32_t gc_data;
	uint32_t gc_mapping;
	uint32_t *compaction_cnt;
	uint32_t compaction_early_invalidation_cnt;

	uint64_t merge_total_entry_cnt;
	uint64_t merge_valid_entry_cnt;
	uint64_t tiering_total_entry_cnt;
	uint64_t tiering_valid_entry_cnt;
	uint64_t max_memory_usage_bit;

	/*time*/
	MeasureTime RH_check_stopwatch[2]; //0 -> leveling 1-> tiering
	MeasureTime RH_make_stopwatch[2]; //0 -> leveling 1-> tiering
}lsmtree_monitor;

typedef struct lsmtree_parameter{
	uint32_t LEVELN;
	uint32_t mapping_num;
	uint32_t last_size_factor;
	uint32_t normal_size_factor;
	uint32_t read_helper_type;
	float read_amplification;
	read_helper_param leveling_rhp;
	read_helper_param tiering_rhp;
	bool version_enable;
	uint32_t plr_bit;
	uint32_t error_range;
	uint32_t version_number;
}lsmtree_parameter;

enum{
	NOCHECK, K2VMAP, DATA, PLR,
};

typedef struct lsmtree_read_param{
	uint32_t check_type;
	int32_t prev_level;
	int32_t prev_run;
	sst_file *prev_sf;
	uint32_t version;
	bool use_read_helper;
	uint32_t read_helper_idx;
	uint32_t piece_ppa;
	read_helper *rh;
	rwlock *target_level_rw_lock;
}lsmtree_read_param;

typedef struct lsmtree{
	uint32_t wb_num;
	page_manager *pm;
	struct compaction_master *cm;
	uint32_t now_merging_run[1+1];
	//page_manager *pm_map;

#ifdef PINKGC
	fdriver_lock_t moved_kp_lock;
	std::deque<key_ptr_pair*>* moved_kp_set;
#endif

	uint32_t now_wb;
	write_buffer **wb_array;
	struct version *last_run_version;
	level **disk;
	
	rwlock *level_rwlock;

	fdriver_lock_t flush_lock;
	lsmtree_parameter param;
	lsmtree_monitor monitor;
	bool global_debug_flag;

	rwlock flush_wait_wb_lock;
	write_buffer *flush_wait_wb;

	rwlock flushed_kp_set_lock;
	key_ptr_pair **flushed_kp_set;

	uint32_t* gc_unavailable_seg;

	lower_info *li;
}lsmtree;

typedef struct page_read_buffer{
	std::map<uint32_t, algo_req *> * pending_req;
	std::map<uint32_t, algo_req *>* issue_req;
	fdriver_lock_t pending_lock;
	fdriver_lock_t read_buffer_lock;
	uint32_t buffer_ppa;
	char buffer_value[PAGESIZE];
}page_read_buffer;


uint32_t lsmtree_argument_set(int argc, char *argv[]);
uint32_t lsmtree_create(lower_info *li, blockmanager *bm, algorithm *);
void lsmtree_destroy(lower_info *li, algorithm *);
uint32_t lsmtree_read(request *const req);
uint32_t lsmtree_write(request *const req);
uint32_t lsmtree_flush(request *const req);
uint32_t lsmtree_remove(request *const req);
void lsmtree_compaction_end_req(struct compaction_req*);
void lsmtree_level_summary(lsmtree *lsm);
void lsmtree_content_print(lsmtree *lsm);
void lsmtree_find_version_with_lock(uint32_t lba, lsmtree_read_param *param);
sst_file *lsmtree_find_target_sst_mapgc(uint32_t lba, uint32_t map_ppa);
void lsmtree_gc_unavailable_set(lsmtree *lsm, sst_file *sptr, uint32_t seg_idx);
void lsmtree_gc_unavailable_unset(lsmtree *lsm, sst_file *sptr, uint32_t seg_idx);
void lsmtree_gc_unavailable_sanity_check(lsmtree *lsm);
uint64_t lsmtree_all_memory_usage(lsmtree *lsm, uint64_t* , uint64_t *, uint32_t);
void lsmtree_tiered_level_all_print();
//sst_file *lsmtree_find_target_sst(uint32_t lba, uint32_t *idx);

#define MAKE_L0COMP_REQ(wb, kp_set, param, is_gc_data)\
	alloc_comp_req(-1,0,(wb), (kp_set),lsmtree_compaction_end_req, (void*)(param), (is_gc_data))

#endif
