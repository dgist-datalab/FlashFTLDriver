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

#define COMPACTION_REQ_MAX_NUM 1
#define WRITEBUFFER_NUM (1+1)
#define TIMERESULT
#define CEILING_TARGET(N, G) ((N)/(G) + (N%G?1:0))

/*OPTIMIZING SET*/
//#define HOT_COLD
#define FAST
#define MIN_ENTRY_PER_SST 128
#define MIN_SEQ_ENTRY_NUM 4
#define WB_SEPARATE
#define INVALIDATION_COUNT_MERGE
#define DYNAMIC_HELPER_ASSIGN

#define DEMAND_SEG_LOCK
#define UPDATING_COMPACTION_DATA
#define DYNAMIC_WISCKEY

enum{
	DEMOTE_RUN, KEEP_RUN,
};

typedef struct lsmtree_monitor{
	/*cnt*/
	uint32_t trivial_move_cnt;
	uint32_t gc_data;
	uint32_t gc_mapping;
	uint32_t *compaction_cnt;
	uint32_t compaction_reclaim_run_num;
	uint32_t compaction_early_invalidation_cnt;

	uint64_t merge_total_entry_cnt;
	uint64_t merge_valid_entry_cnt;
	uint64_t max_memory_usage_bit;
	uint64_t flushed_kp_num;
	uint64_t flushing_sequential_file;
	uint64_t flushing_random_file;
	uint64_t tiering_valid_entry_cnt[10];
	uint64_t tiering_total_entry_cnt[10];

	/*time*/
	MeasureTime RH_check_stopwatch[2]; //0 -> leveling 1-> tiering
	MeasureTime RH_make_stopwatch[2]; //0 -> leveling 1-> tiering
}lsmtree_monitor;

typedef struct level_param{
	uint32_t level_type;
	bool is_wisckey;
	bool is_bf;
}level_param;

typedef struct tree_param{
	bool isinvalid;
	double size_factor;
	uint32_t num_of_level;
	level_param *lp;
	uint64_t memory_usage_bit;
	uint64_t run_num;
	uint64_t total_run_num_bit;
	uint64_t entry_bit;
	uint64_t entry_num[21];
	uint32_t plr_cnt;
//	double run_num;
	double bloom_bit;
	double plr_bit;
	double avg_run_size;
	double rh_bit[21];
	double run_range_size[21];
	double level_size_ratio[21];
	double WAF;
#ifdef DYNAMIC_WISCKEY
	uint64_t BF_memory;
	uint64_t PLR_memory;
	uint64_t memory_limit_for_helper;
#endif
}tree_param;

typedef struct lsmtree_parameter{
	uint32_t LEVELN;
	uint32_t mapping_num;
	double last_size_factor;
	double normal_size_factor;
	uint32_t write_buffer_bit;
	uint32_t write_buffer_ent;
#ifdef DYNAMIC_HELPER_ASSIGN
	float BF_PLR_border;
#endif

#ifdef MIN_ENTRY_PER_SST
	uint32_t max_sst_in_pinned_level;
#endif
	uint32_t reclaim_ppa_target;
	float read_amplification;

	read_helper_param bf_ptr_guard_rhp;
	read_helper_param bf_guard_rhp;
	read_helper_param plr_rhp;

	bool version_enable;
	tree_param tr;
}lsmtree_parameter;

enum{
	NOCHECK, K2VMAP, DATA, PLR,
};

typedef struct guard_range{
	uint32_t start_lba;
	uint32_t end_lba;
	uint64_t flushing_cnt;
}guard_range;

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

	uint32_t now_wb;
	write_buffer **wb_array;
	struct version *last_run_version;
	struct level **disk;
	struct level *pinned_level;

#ifdef MIN_ENTRY_PER_SST
	bool sst_sequential_available_flag;
	uint32_t randomness_check;
	int32_t now_pinned_sst_file_num;
	uint32_t processing_lba;
	uint32_t processed_entry_num;
	struct run * unaligned_sst_file_set;
#endif
	
	uint32_t same_segment_flag;
	uint32_t same_target_segment;
	rwlock *level_rwlock;

	fdriver_lock_t flush_lock;
	lsmtree_parameter param;
	lsmtree_monitor monitor;
	bool global_debug_flag;
	bool function_test_flag;

	rwlock flush_wait_wb_lock;
	write_buffer *flush_wait_wb;

	rwlock flushed_kp_set_lock;
	std::set<uint32_t> *flushed_kp_seg;
	std::map<uint32_t, uint32_t> *flushed_kp_set;

#ifdef WB_SEPARATE
	std::map<uint32_t, uint32_t> *hot_kp_set;
#endif
	std::map<uint32_t, uint32_t> *flushed_kp_temp_set;

	struct read_issue_arg **read_arg_set;
	struct sst_bf_out_stream *now_compaction_bos;
	uint32_t now_compaction_stream_num;
	std::queue<uint32_t> gc_moved_map_ppa;
	struct sst_pf_out_stream** compactioning_pos_set;

	fdriver_lock_t gc_unavailable_seg_lock;
	uint32_t* gc_unavailable_seg;
	uint32_t* blocked_invalidation_seg;
	uint32_t gc_locked_seg_num;

	uint32_t now_gc_seg_idx;
	uint32_t now_read_issue_seg_idx;
	fdriver_lock_t gc_end_lock;
	fdriver_lock_t now_gc_seg_lock;

#ifdef DYNAMIC_WISCKEY
	bool next_level_wisckey_compaction;
#endif

	lower_info *li;
#ifdef LSM_DEBUG
	uint32_t *LBA_cnt;
#endif
}lsmtree;

typedef struct page_read_buffer{
	std::multimap<uint32_t, algo_req *> * pending_req;
	std::multimap<uint32_t, algo_req *>* issue_req;
	fdriver_lock_t pending_lock;
	fdriver_lock_t read_buffer_lock;
	uint32_t buffer_ppa;
	char buffer_value[PAGESIZE];
}page_read_buffer;

lsmtree_parameter lsmtree_memory_limit_to_setting(uint64_t memory_limit_bit);
uint64_t lsmtree_memory_limit_of_WAF(uint32_t WAF, double *line_per_chunk, double *);
uint32_t lsmtree_argument_set(int argc, char *argv[]);
uint32_t lsmtree_create(lower_info *li, blockmanager *bm, algorithm *);
void lsmtree_destroy(lower_info *li, algorithm *);
uint32_t lsmtree_read(request *const req);
uint32_t lsmtree_write(request *const req);
uint32_t lsmtree_flush(request *const req);
uint32_t lsmtree_remove(request *const req);
void lsmtree_compaction_end_req(struct compaction_req*);
void lsmtree_level_summary(lsmtree *lsm, bool force);
void lsmtree_content_print(lsmtree *lsm, bool print_sst);
void lsmtree_find_version_with_lock(uint32_t lba, lsmtree_read_param *param);
sst_file *lsmtree_find_target_sst_mapgc(uint32_t lba, uint32_t map_ppa);
sst_file *lsmtree_find_target_normal_sst_datagc(uint32_t lba, uint32_t map_ppa,
		uint32_t *lev_idx, uint32_t *target_version, uint32_t* target_sidx);
sst_file **lsmtree_fine_target_wisckey_sst_set(uint32_t lba);
void lsmtree_gc_unavailable_set(lsmtree *lsm, sst_file *sptr, uint32_t seg_idx);
void lsmtree_gc_unavailable_unset(lsmtree *lsm, sst_file *sptr, uint32_t seg_idx);
void lsmtree_gc_unavailable_sanity_check(lsmtree *lsm);
uint64_t lsmtree_all_memory_usage(lsmtree *lsm, uint64_t* , uint64_t *, uint32_t, uint64_t *);
void lsmtree_tiered_level_all_print();
void lsmtree_gc_lock_level(lsmtree *lsm, uint32_t level_idx);
void lsmtree_gc_unlock_level(lsmtree *lsm, uint32_t level_idx);

void lsmtree_block_already_gc_seg(lsmtree *lsm, uint32_t seg);
void lsmtree_unblock_already_gc_seg(lsmtree *lsm);
void lsmtree_control_gc_lock_on_read(lsmtree *lsm, uint32_t piece_ppa, bool _final);
void lsmtree_after_compaction_processing(lsmtree *lsm);
uint32_t lsmtree_print_log();
uint32_t lsmtree_total_invalidate_num(lsmtree *lsm);

uint32_t lsmtree_testing();
uint32_t lsmtree_seg_debug(lsmtree *lsm);
uint32_t lsmtree_get_seg_invalidate_number();
bool lsmtree_target_run_wisckeyable(uint32_t run_contents_num, bool bf_helper);

bool invalidate_kp_entry(uint32_t lba, uint32_t piece_ppa, uint32_t old_version, bool aborting);
static inline bool lsmtree_is_gc_unavailable(lsmtree *lsm, uint32_t seg_idx){
	bool res;
	fdriver_lock(&lsm->gc_unavailable_seg_lock);
	res=lsm->gc_unavailable_seg[seg_idx]!=0?true:false;
	fdriver_unlock(&lsm->gc_unavailable_seg_lock);
	return res;
}
static void lsmtree_print_WAF(lower_info *li){
	printf("WAF: %lf\n\n",
			(double)(li->req_type_cnt[MAPPINGW] +
				li->req_type_cnt[DATAW]+
				li->req_type_cnt[GCDW]+
				li->req_type_cnt[GCMW_DGC]+
				li->req_type_cnt[COMPACTIONDATAW])/li->req_type_cnt[DATAW]);
}

//sst_file *lsmtree_find_target_sst(uint32_t lba, uint32_t *idx);
read_helper_param lsmtree_get_target_rhp(uint32_t level_idx);
void lsmtree_init_ordering_param();
void lsmtree_compactioning_set_print(uint32_t seg_idx);
void lsmtree_compactioning_set_gced_flag(uint32_t seg_idx);
void lsmtree_tiering_memory_waf_calculator(uint32_t limit_round);
#define MAKE_L0COMP_REQ(wb, param, is_gc_data)\
	alloc_comp_req(-1,0,(wb),lsmtree_compaction_end_req, (void*)(param), (is_gc_data))

#endif
