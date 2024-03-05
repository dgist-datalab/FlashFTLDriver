#ifndef __DEMAND_MAPPING_H__
#define __DEMAND_MAPPING_H__
#include "demand_io.h"
#include "my_cache.h"
#include "../../include/data_struct/list.h"
#include "../../include/sem_lock.h"
#include "../../include/container.h"
#include "../../include/data_struct/bitmap.h"
#include <map>
#include <list>

//#define CHECKING_TEST_TIME

#define GETGTDIDX(lba) ((lba)/(PAGESIZE/sizeof(DMF)))
#define TRANSOFFSET(lba) ((lba)%(PAGESIZE/sizeof(DMF)))
#define GTDNUM (RANGE/(PAGESIZE/sizeof(DMF)) + (RANGE%(PAGESIZE/sizeof(DMF))?1:0))
#define GETETR(dmm, lba) (&(dmm).GTD[GETGTDIDX(lba)])
#define GTD_IDX_TO_FIRST_LBA(idx) ((idx)*PAGESIZE/sizeof(DMF))
typedef enum {
	DEMAND_COARSE, DEMAND_FINE, SFTL, TPFTL, NOCPU_SFTL
}cache_algo_type;

enum{
	MAP_READ_ISSUE_END, FLYING_HIT_END, RETRY_END, MAP_WRITE_END, 
	DONE_END, MISS_STATUS_DONE, NOTFOUND_END, NO_EVICTIONING_ENTRY
};

enum{
	 HAVE_SPACE, NORMAL_EVICTION, EMPTY_EVICTION,
};

enum{
	MAP_MISS=1,
	MAP_EVICT_READ=2,
	MAP_WRITE=4,
	MAP_WRITE_GC=8,
};
#define CACHE_TYPE_MAX_NUM (NOCPU_SFTL+1)

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
	uint32_t idx;
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

typedef struct demand_param{
	MAP_ASSIGN_STATUS status;
	MAP_ASSIGN_STATUS prev_status[10];
	uint32_t now_eviction_hint;
	uint32_t log;
	evict_target et;
	mapping_entry target;
	void *param_ex;
	void *cache_private;
	bool is_hit_eviction;
	bool is_fresh_miss;

	uint32_t flying_map_read_key;

	MeasureTime mt;

	uint8_t write_w_num;
	uint8_t write_r_num;
	uint8_t write_d_num;

	uint8_t read_w_num;
	uint8_t read_r_num;
	uint8_t read_d_num;
}demand_param;

typedef struct assign_param_ex{
	KEYT *lba;
	KEYT *physical;
	uint32_t *prefetching_info;
	uint32_t max_idx;
	uint8_t idx;
}assign_param_ex;

typedef struct demand_map_manager{
	uint32_t max_caching_pages;
	cache_algo_type c_type;
	GTD_entry *GTD;	
	my_cache *cache;
	lower_info *li;
	uint32_t eviction_hint;
	//uint32_t eviction_req_cnt;
	bool global_debug_flag;
	uint32_t now_mapping_read_cnt;
	//uint32_t flying_lba_idx;
	//uint32_t flying_lba_array[QDEPTH*2];
	fdriver_lock_t flying_map_read_lock;
	std::map<uint32_t, request*> *flying_map_read_req_set;
	std::map<uint32_t, bool> *flying_map_read_flag_set;
	std::map<uint32_t, request*> *flying_req;//pair<req->seq, request*>
	std::map<uint32_t, request*> *all_now_req;//pair<req->seq, request*>
	std::list<request *> *stopped_request;
	blockmanager *bm;
}demand_map_manager;


typedef struct gc_map_value{
	uint32_t gtd_idx;
	uint32_t start_idx;
	value_set *value;
	bool isdone;
	mapping_entry pair;
}gc_map_value;

typedef struct demand_map_moniter{
	uint32_t hit_num;
	uint32_t write_hit_num;
	uint32_t read_hit_num;
	uint32_t write_shadow_hit_num;
	uint32_t read_shadow_hit_num;
	uint32_t eviction_shadow_hit_num;

	uint32_t miss_num;
	uint32_t total_cold_miss_num;
	uint32_t write_cold_miss_num;	
	uint32_t read_cold_miss_num;	
	uint32_t write_miss_num;
	uint32_t read_miss_num;

	uint32_t additional_eviction_cnt;
	uint32_t eviction_cnt;
	uint32_t hit_eviction;
	uint32_t dirty_eviction;
	uint32_t clean_eviction;

	uint32_t write_cnt;
	uint32_t write_data[10][10][10];
	uint64_t write_time[10][10][10];
	uint32_t read_cnt;
	uint32_t read_data[10][10][10];
	uint64_t read_time[10][10][10];

	bitmap *read_working_set;
	uint32_t read_working_set_num;

	bitmap *write_working_set;
	uint32_t write_working_set_num;
}dmi;

uint32_t demand_argument(int argc, char **argv);
void demand_map_create(uint32_t total_caching_physical_pages, lower_info *, blockmanager *);
uint32_t demand_map_assign(request *req, KEYT *lba, KEYT *physical, uint32_t *prefetching_info, bool remove);
uint32_t demand_map_some_update(mapping_entry *, uint32_t idx); //for gc
uint32_t demand_page_read(request *const req);
uint32_t demand_map_remove(request *const req);
uint32_t demand_print_log();

void demand_map_free();
void demand_eviction(void *);

#endif
