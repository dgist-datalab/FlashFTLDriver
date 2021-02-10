#ifndef __LSMTREE_H__
#define __LSMTREE_H__

#include "../../include/container.h"
#include "page_manager.h"
#include "write_buffer.h"
#include "version.h"

typedef struct lsmtree_monitor{

}lsmtree_monitor;

typedef struct lsmtree_parameter{
	uint32_t LEVELN;
}lsmtree_parameter;

typedef struct lsmtree{
	uint32_t wb_num;
	page_manager *pm;

	uint32_t now_wb;
	write_buffer **wb_array;
	version *last_run_versions;
	level *lev;
	lsmtree_paramter param;
}lsmtree;


uint32_t lsmtree_argument_set(int argc, char *argv[]);
uint32_t lsmtree_create(lower_info *li, blockmanager *bm, algorithm *);
void lsmtree_destroy(lower_info *li, algorithm *);
uint32_t lsmtree_read(request *const req);
uint32_t lsmtree_write(request *const req);
uint32_t lsmtree_flush(request *const req);
uint32_t lsmtree_remove(request *const req);

#endif
