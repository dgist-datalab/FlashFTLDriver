#pragma once
#include "../../include/settings.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./group.h"
#include <map>

#define COMPACTION_RESOLUTION 1000000
typedef struct page_read_buffer{
	std::multimap<uint32_t, algo_req *> * pending_req;
	std::multimap<uint32_t, algo_req *>* issue_req;
	fdriver_lock_t pending_lock;
	fdriver_lock_t read_buffer_lock;
	uint32_t buffer_ppa;
	char buffer_value[PAGESIZE];
}page_read_buffer;
typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;

uint32_t lea_create(lower_info *, blockmanager *, algorithm *);
void lea_destroy(lower_info *, algorithm *);
uint32_t lea_argument(int argc, char **argv);
uint32_t lea_read(request *const);
uint32_t lea_write(request *const);
uint32_t lea_remove(request *const);

uint32_t *lea_gp_to_mapping(group *gp);
void lea_cache_insert(group *gp, uint32_t *piece_ppa);
bool lea_cache_evict(group *gp);
void lea_cache_promote(group *gp);
void lea_cache_size_update(group *gp, uint32_t size, bool decrease);
void lea_cache_evict_force();