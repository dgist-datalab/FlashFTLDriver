#ifndef __LSMTREE_H__
#define __LSMTREE_H__

#include "../../include/sem_lock.h"
#include "../../include/container.h"
#include "../../include/utils/rwlock.h"
#include "page_manager.h"
#include "write_buffer.h"
#include "version.h"
#include "level.h"
#include "gc.h"
#include "compaction.h"
#include "lsmtree_param_module.h"
#include "read_helper.h"
#include "version.h"

#define TARGETFPR 0.1f

typedef struct lsmtree_monitor{
	uint32_t trivial_move_cnt;
	uint32_t *compaction_cnt;
}lsmtree_monitor;

typedef struct lsmtree_parameter{
	uint32_t LEVELN;
	uint32_t mapping_num;
	uint32_t size_factor;	
	read_helper_param leveling_rhp;
	read_helper_param tiering_rhp;
}lsmtree_parameter;

enum{
	NOCHECK, K2VMAP, DATA, PLR,
};

typedef struct lsmtree_read_param{
	uint32_t check_type;
	int32_t prev_level;
	int32_t prev_run;
	sst_file *prev_sf;
	bool use_read_helper;
	uint32_t read_helper_idx;
	rwlock *target_level_rw_lock;
}lsmtree_read_param;

typedef struct lsmtree{
	uint32_t wb_num;
	page_manager *pm;
	struct compaction_master *cm;
	//page_manager *pm_map;

	std::queue<key_ptr_pair*>* moved_kp_set;

	uint32_t now_wb;
	write_buffer **wb_array;
	struct version *last_run_version;
	level **disk;
	
	rwlock *level_rwlock;

	lsmtree_parameter param;
	lsmtree_monitor monitor;
	bool global_debug_flag;
	lower_info *li;
}lsmtree;


uint32_t lsmtree_argument_set(int argc, char *argv[]);
uint32_t lsmtree_create(lower_info *li, blockmanager *bm, algorithm *);
void lsmtree_destroy(lower_info *li, algorithm *);
uint32_t lsmtree_read(request *const req);
uint32_t lsmtree_write(request *const req);
uint32_t lsmtree_flush(request *const req);
uint32_t lsmtree_remove(request *const req);
void lsmtree_compaction_end_req(struct compaction_req*);
void lsmtree_level_summary(lsmtree *lsm);
sst_file *lsmtree_find_target_sst_mapgc(uint32_t lba, uint32_t map_ppa);
//sst_file *lsmtree_find_target_sst(uint32_t lba, uint32_t *idx);

#define MAKE_L0COMP_REQ(kp_set, param)\
	alloc_comp_req(-1,0,(kp_set), lsmtree_compaction_end_req, (void*)(param))

#endif
