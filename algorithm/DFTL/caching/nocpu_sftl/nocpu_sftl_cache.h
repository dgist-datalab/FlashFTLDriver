#ifndef __SFTL_CACHE_HEADER__
#define __SFTL_CACHE_HEADER__
#define FASTSFTLCPU
#include "../../my_cache.h"
#include "../../demand_mapping.h"
#include "../../../../include/data_struct/lru_list.h"
#include "../../../../include/data_struct/bitmap.h"
#include <map>
#include <vector>

#define BITMAPFROMLN(ln) (((nocpu_sftl_cache*)((ln)->data))->map)
#define HEADARRFROMLN(ln) (((nocpu_sftl_cache*)((ln)->data))->head_array)
#define GETSCFROMETR(etr) ((nocpu_sftl_cache*)((lru_node*)etr->private_data)->data)
#define BITMAPMEMBER (PAGESIZE/sizeof(DMF))
#define BITMAPSIZE (BITMAPMEMBER/8+(BITMAPMEMBER%8?1:0))

#define GETOFFSET(lba) TRANSOFFSET(lba)
#define ISLASTOFFSET(lba) ((GETOFFSET(lba))==PAGESIZE/sizeof(uint32_t)-1)

typedef struct nocpu_sftl_cache{
	uint32_t *head_array;
	GTD_entry *etr;
	std::map<int,int> *run_length; //start end
	uint32_t unpopulated_num;
}nocpu_sftl_cache;

typedef struct nocpu_sftl_cache_monitor{
	uint32_t max_caching_byte;
	uint32_t now_caching_byte;
	uint32_t *gtd_size;
	LRU *lru;
	std::vector<nocpu_sftl_cache> temp_ent;
}nocpu_sftl_cache_monitor;

uint32_t nocpu_sftl_init(struct my_cache *, uint32_t total_caching_physical_pages);
uint32_t nocpu_sftl_free(struct my_cache *);
uint32_t nocpu_sftl_is_needed_eviction(struct my_cache *, uint32_t lba, uint32_t *, uint32_t eviction_hint);
uint32_t nocpu_sftl_update_eviction_hint(struct my_cache *, uint32_t lba, uint32_t *prefetching_info, uint32_t eviction_hint, 
		uint32_t *now_eviction_hint, bool increase);

uint32_t nocpu_sftl_update_hit_eviction_hint(struct my_cache *, uint32_t lba,  uint32_t *prefetching_info, uint32_t eviction_hint, uint32_t *now_eviction_hint, bool increase);
uint32_t nocpu_sftl_update_entry(struct my_cache *, GTD_entry *, uint32_t lba, uint32_t ppa, uint32_t *eviction_hint);
uint32_t nocpu_sftl_update_entry_gc(struct my_cache *, GTD_entry *, uint32_t lba, uint32_t ppa);
uint32_t nocpu_sftl_insert_entry_from_translation(struct my_cache *, GTD_entry *, uint32_t lba, char *data, uint32_t *eviction_hint, uint32_t org_eviction_hint);
uint32_t nocpu_sftl_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa);
uint32_t nocpu_sftl_get_mapping(struct my_cache *, uint32_t lba);
struct GTD_entry *nocpu_sftl_get_eviction_GTD_entry(struct my_cache *, uint32_t lba);//if return value is NULL, it is clean eviction.
bool nocpu_sftl_update_eviction_target_translation(struct my_cache* , uint32_t, GTD_entry *etr, mapping_entry * map, char *data, void *, bool);
bool nocpu_sftl_exist(struct my_cache *, uint32_t lba);
void nocpu_sftl_update_dynamic_size(struct my_cache *, uint32_t lba,char *data);

void nocpu_sftl_mapping_verify(nocpu_sftl_cache* sc);
void nocpu_sftl_print_mapping(nocpu_sftl_cache* sc);
uint32_t nocpu_sftl_print_mapping_target(nocpu_sftl_cache *sc, uint32_t lba);

bool nocpu_sftl_is_hit_eviction(struct my_cache *, GTD_entry *,uint32_t lba, uint32_t ppa, uint32_t total_hit_eviction);
void nocpu_sftl_force_put_mru(struct my_cache *, GTD_entry *, mapping_entry *, uint32_t);
bool nocpu_sftl_is_eviction_hint_full(struct my_cache *, uint32_t eviction_hint);
int32_t nocpu_sftl_get_remain_space(struct my_cache *, uint32_t total_eviction_hint);
bool nocpu_sftl_dump_cache_update(struct my_cache *, GTD_entry *etr, char *data);
void nocpu_sftl_load_specialized_meta(struct my_cache *, GTD_entry *etr, char *data);
void nocpu_sftl_empty_cache(struct my_cache *);
#endif
