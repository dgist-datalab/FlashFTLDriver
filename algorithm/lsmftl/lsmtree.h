#ifndef __LSMTREE_H__
#define __LSMTREE_H__

#include "../../include/sem_lock.h"
#include "../../include/container.h"
#include "page_manager.h"
#include "write_buffer.h"
#include "version.h"
#include "level.h"
#include "gc.h"
#include "compaction.h"
#include "lsmtree_param_module.h"

typedef struct lsmtree_monitor{
	uint32_t trivial_move_cnt;
	uint32_t *compaction_cnt;
}lsmtree_monitor;

typedef struct lsmtree_parameter{
	uint32_t LEVELN;
	uint32_t mapping_num;
	uint32_t size_factor;
	void *private_data_for_readhelper;
}lsmtree_parameter;

enum{
	NOCHECK, K2VMAP, DATA, PLR,
};

typedef struct lsmtree_read_param{
	uint32_t check_type;
	int32_t prev_level;
	int32_t prev_run;
	int32_t sst_map_idx;
}lsmtree_read_param;

typedef struct lsmtree{
	uint32_t wb_num;
	page_manager *pm;
	struct compaction_master *cm;
	//page_manager *pm_map;

	uint32_t now_wb;
	write_buffer **wb_array;
	uint32_t version_num;
	version *last_run_version;
	level **disk;
	fdriver_lock_t *level_lock;
	lsmtree_parameter param;
	lsmtree_monitor monitor;
	bool global_debug_flag;
}lsmtree;


uint32_t lsmtree_argument_set(int argc, char *argv[]);
uint32_t lsmtree_create(lower_info *li, blockmanager *bm, algorithm *);
void lsmtree_destroy(lower_info *li, algorithm *);
uint32_t lsmtree_read(request *const req);
uint32_t lsmtree_write(request *const req);
uint32_t lsmtree_flush(request *const req);
uint32_t lsmtree_remove(request *const req);
void lsmtree_compaction_end_req(struct compaction_req*);

#define MAKE_L0COMP_REQ(kp_set, param)\
	alloc_comp_req(-1,0,(kp_set), lsmtree_compaction_end_req, (void*)(param))

#endif
