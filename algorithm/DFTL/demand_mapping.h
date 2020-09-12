#ifndef __DEMAND_MAPPING_H__
#define __DEMAND_MAPPING_H__
#include "demand_io.h"
#include "my_cache.h"
#include "../../include/data_struct/list.h"
#include "../../include/sem_lock.h"
#include "../../include/container.h"


#define GETGTDIDX(lba) (lba/(PAGESIZE/sizeof(DMF)))
#define TRANSOFFSET(lba) (lba%(PAGESIZE/sizeof(DMF)))

typedef enum GTD_ETR_STATUS{
	EMPTY				= 0x0000,
	POPULATE,CLEAN,DIRTY,FLYING,EVICTING,
}GTD_ETR_STATUS;

typedef struct demand_map_format{
	KEYT ppa;
}DMF; 

typedef struct mapping_entry{
	KEYT lba;
	KEYT ppa;
	void *private_data;
}mapping_entry;

typedef enum{
	NONE, HIT, EVICTIONW, EVICTIONR, MISSR 
}MAP_ASSIGN_STATUS;

typedef struct GTD_entry{
	uint32_t physical_address;
	GTD_ETR_STATUS status;
	fdriver_lock_t lock;
	list *pending_req;
	void *private_data;
}GTD_entry;

typedef union evict_target{
	void *ptr;
	GTD_entry *gtd;
	mapping_entry *mapping;
}evict_target;

typedef struct demand_params{
	MAP_ASSIGN_STATUS status;
	GTD_entry *etr;
	evict_target et;
	mapping_entry target;

	void *params_ex;
}demand_params;

typedef struct assign_params_ex{
	KEYT *lba;
	KEYT *physical;
	uint8_t idx;
}assign_params_ex;


typedef struct pick_params_ex{
	KEYT lba;
}pick_params_ex;

typedef struct demand_map_manager{
	uint32_t max_caching_pages;
	GTD_entry *GTD;	
	my_cache *cache;
	lower_info *li;
	blockmanager *bm;
}demand_map_manager;


typedef struct gc_map_value{
	uint32_t gtd_idx;
	uint32_t start_idx;
	value_set *value;
	bool isdone;
	mapping_entry pair;
}gc_map_value;

void demand_map_create(uint32_t total_caching_physical_pages, lower_info *, blockmanager *);
uint32_t demand_map_assign(request *req, KEYT *lba, KEYT *physical);
uint32_t demand_map_some_update(mapping_entry *, uint32_t idx); //for gc
uint32_t demand_page_read(request *const req);

void demand_map_free();
void demand_eviction(void *);


#endif
