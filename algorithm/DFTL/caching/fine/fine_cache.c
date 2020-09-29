#include "fine_cache.h"
#include "../../demand_mapping.h"
#include "../../gc.h"
#include "../../../../include/settings.h"
#include "../../../../include/data_struct/bitmap.h"
#include "../../../../include/dftl_settings.h"
#include <stdio.h>
#include <stdlib.h>

extern uint32_t test_key;
extern demand_map_manager dmm;
fine_cache_monitor fcm;

my_cache fine_cache_func{
	.init=fine_init,
	.free=fine_free,
	.is_needed_eviction=fine_is_needed_eviction,
	.need_more_eviction=NULL,
	.is_hit_eviction=NULL,
	.update_entry=fine_update_entry,
	.update_entry_gc=fine_update_entry_gc,
	.force_put_mru=fine_force_put_mru,
	.insert_entry_from_translation=fine_insert_entry_from_translation,
	.update_from_translation_gc=fine_update_from_translation_gc,
	.get_mapping=fine_get_mapping,
	.get_eviction_GTD_entry=NULL,
	.get_eviction_mapping_entry=fine_get_eviction_entry,
	.update_eviction_target_translation=fine_update_eviction_target_translation,
	.evict_target=fine_evict_target, 
	.update_dynamic_size=NULL,
	.exist=fine_exist,
};

uint32_t fine_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&fcm.lru, NULL);
	fcm.max_caching_map=(total_caching_physical_pages*PAGESIZE/(sizeof(uint32_t)*2));
	fcm.now_caching_map=0;
	mc->type=FINE;
	mc->private_data=NULL;

#ifdef SEARCHSPEEDUP
	cache_node_lru_mapping* cl_mapping;
	cl_mapping=(cache_node_lru_mapping*)malloc(sizeof(cache_node_lru_mapping));
	cl_mapping->fc_array=(void**)calloc(TOTALLPN, sizeof(void*));
	fcm.cl_mapping=cl_mapping;
#endif

	fcm.GTD_internal_state=(char*)calloc((TOTALLPN/(PAGESIZE/sizeof(DMF)))+(TOTALLPN%(PAGESIZE/sizeof(DMF))?1:0),sizeof(char));
	fcm.populated_cache_entry=bitmap_init(TOTALLPN);

	return fcm.max_caching_map;
}

uint32_t fine_free(struct my_cache *mc){
	while(1){
		fine_cache *fc=(fine_cache*)lru_pop(fcm.lru);
		if(!fc) break;
		free(fc->private_data);
		free(fc);
	}
	free(mc->private_data);
	lru_free(fcm.lru);
	return 1;
}

bool fine_is_needed_eviction(struct my_cache *mc, uint32_t ){
	if(fcm.max_caching_map==fcm.now_caching_map) return true;
	if(fcm.max_caching_map < fcm.now_caching_map){
		printf("now caching map bigger!!!! %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	return false;
}

static inline void checking_lba_exist(uint32_t lba){
	/*
	lru_node *target;
	for_each_lru_list(fcm.lru, target){
		fine_cache *fc=(fine_cache*)target->data;
		if(fc->lba==lba){
		//	printf("find lba! in lru\n");	
			return;
		}
	}*/
	//printf("not find in lba");
}

static inline fine_cache * __find_lru_map(uint32_t lba){
#ifdef SEARCHSPEEDUP
	return (fine_cache*)fcm.cl_mapping->fc_array[lba];
#else
	lru_node *ln;
	fine_cache *fc;
	for_each_lru_list(fcm.lru, ln){
		fc=(fine_cache*)ln->data;
		if(fc->lba==lba) return fc;
	}
#endif
	return NULL;
}

static inline uint32_t __update_entry(GTD_entry *etr,uint32_t lba, uint32_t ppa, bool isgc){
	fine_cache *fc;
	fine_cache_node *fcn;
	uint32_t old_ppa=UINT32_MAX;

	if((fc=__find_lru_map(lba))==NULL){
		if(isgc) return old_ppa;
		fc=(fine_cache*)malloc(sizeof(fine_cache));
		fc->ppa=ppa;
		fc->lba=lba;
#ifdef SEARCHSPEEDUP
		fcm.cl_mapping->fc_array[lba]=(void*)fc;
#endif
		fcn=(fine_cache_node*)malloc(sizeof(fine_cache_node));
		fcn->lru_node=lru_push(fcm.lru, fc);
		fc->private_data=(void*)fcn;

		bitmap_set(fcm.populated_cache_entry, lba);
		fcm.now_caching_map++;
		if(fcm.now_caching_map > fcm.max_caching_map){
			printf("caching overflow! %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		goto last;
	}

	old_ppa=fc->ppa;
	fc->ppa=ppa;
	
last:
	if(lba==1652090){
		printf("%u %u->%u\n", lba, old_ppa, ppa);
	}
	set_flag(fc,1);
	if(!isgc){
		lru_update(fcm.lru, get_ln(fc));
	}
	return old_ppa;
}

uint32_t fine_update_entry(struct my_cache *mc, GTD_entry *e, uint32_t lba, uint32_t ppa){
	return __update_entry(e, lba, ppa, false);
}

uint32_t fine_update_entry_gc(struct my_cache *mc, GTD_entry *e, uint32_t lba, uint32_t ppa){
	return __update_entry(e, lba, ppa, true);
}

uint32_t fine_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t lba, char *data){
	if(etr->status==EMPTY){
		printf("try to read not populated entry! %s:%d\n",__FILE__, __LINE__);
		abort();
	}
	
	fine_cache *fc=(fine_cache*)malloc(sizeof(fine_cache));
	uint32_t *map=(uint32_t*)data;
	fc->lba=lba;
	fc->ppa=map[GETOFFSET(lba)];

	init_fcn(fc);
#ifdef SEARCHSPEEDUP
	fcm.cl_mapping->fc_array[lba]=(void*)fc;
#endif
	bitmap_set(fcm.populated_cache_entry, lba);
	get_ln(fc)=lru_push(fcm.lru, (void*)fc);
	set_flag(fc,0);

	fcm.now_caching_map++;
	return 1;
}

uint32_t fine_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa){
	uint32_t *ppa_list=(uint32_t*)data;
	uint32_t old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
	return old_ppa;
}

uint32_t fine_get_mapping(struct my_cache *, uint32_t lba){
	fine_cache *fc=((fine_cache*)__find_lru_map(lba));
	lru_update(fcm.lru, get_ln(fc));
	return fc->ppa;
}

mapping_entry *fine_get_eviction_entry(struct my_cache *, uint32_t lba){
	lru_node *target;
	//checking_lba_exist(1778630);
	for_each_lru_backword(fcm.lru, target){
		fine_cache *fc=(fine_cache*)target->data;
		if(get_flag(fc)==0){
			lru_delete(fcm.lru, get_ln(fc));
#ifdef SEARCHSPEEDUP
			fcm.cl_mapping->fc_array[fc->lba]=NULL;
#endif
			bitmap_unset(fcm.populated_cache_entry, fc->lba);
			free(fc->private_data);
			free(fc);
			fcm.now_caching_map--;
			return NULL;
		}
		else{
			return fc;
		}
	}
	return NULL;
}

bool fine_update_eviction_target_translation(struct my_cache* ,uint32_t,  GTD_entry *etr, mapping_entry *map, char *data){
	uint32_t gtd_idx=GETGTDIDX(map->lba);
	if(fcm.GTD_internal_state[gtd_idx]==0){
		fcm.GTD_internal_state[gtd_idx]=1;
		memset(data, -1, PAGESIZE);
	}
	uint32_t *ppa_list=(uint32_t*)data;
	fine_cache *fc;
	bool debug_flag=false;
	if(etr->idx==1652090/2048){
		printf("break!\n");
		debug_flag=true;
	}
	uint32_t old_ppa;
#ifdef SEARCHSPEEDUP
	uint32_t unit=PAGESIZE/sizeof(DMF);
	for(uint32_t i=0; i<unit; i++){
		fc=__find_lru_map(gtd_idx*unit+i);
		if(!fc || !get_flag(fc)) continue;
		if(debug_flag && fc->lba==1652090){
			printf("break2\n");
		}
		old_ppa=ppa_list[GETOFFSET(fc->lba)];
		ppa_list[GETOFFSET(fc->lba)]=fc->ppa;
		/*
		if(old_ppa!=UINT32_MAX && !dmm.bm->is_invalid_page(dmm.bm, old_ppa)){
			invalidate_ppa(old_ppa);
		}*/
		set_flag(fc,0);
	}
#else
	lru_node *target;
	for_each_lru_list(fcm.lru, target){
		fc=(fine_cache*)target->data;
		if(GETGTDIDX(fc->lba)!=gtd_idx) continue;
		if(!get_flag(fc)) continue;
		old_ppa=ppa_list[GETOFFSET(fc->lba)];
		ppa_list[GETOFFSET(fc->lba)]=fc->ppa;
		/*
		if(old_ppa!=UINT32_MAX && !dmm.bm->is_invalid_page(dmm.bm, old_ppa)){
			invalidate_ppa(old_ppa);
		}*/
		set_flag(fc,0);
	}
#endif

#ifdef SEARCHSPEEDUP
	fcm.cl_mapping->fc_array[map->lba]=NULL;
#endif

	bitmap_unset(fcm.populated_cache_entry, map->lba);
	fcm.now_caching_map--;
	return  true;
}


bool fine_evict_target(struct my_cache *, GTD_entry *, mapping_entry *fc){
	lru_delete(fcm.lru, get_ln(fc));
#ifdef SEARCHSPEEDUP
	fcm.cl_mapping->fc_array[fc->lba]=NULL;
#endif
	bitmap_unset(fcm.populated_cache_entry, fc->lba);
	free(fc->private_data);
	free(fc);

	fcm.now_caching_map--;
	return true;
}

bool fine_exist(struct my_cache *, uint32_t lba){
	bool res=bitmap_is_set(fcm.populated_cache_entry, lba);
	if(res){
		if(!fcm.cl_mapping->fc_array[lba]){
			printf("bitmap is wiered! %u\n",lba);
			abort();
		}
	}
	return res;
}

void fine_force_put_mru(struct my_cache *, GTD_entry *,mapping_entry *map,  uint32_t lba){
	lru_update(fcm.lru, get_ln(map));
}
