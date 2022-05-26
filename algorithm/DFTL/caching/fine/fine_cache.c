#include "fine_cache.h"
#include "../../demand_mapping.h"
#include "../../gc.h"
#include "../../../../include/settings.h"
#include "../../../../include/data_struct/bitmap.h"
#include "../../../../include/dftl_settings.h"
#include <stdio.h>
#include <stdlib.h>

extern algorithm demand_ftl;
extern uint32_t test_key;
extern demand_map_manager dmm;
extern uint32_t debug_lba;
fine_cache_monitor fcm;

my_cache fine_cache_func{
	.init=fine_init,
	.free=fine_free,
	.is_needed_eviction=fine_is_needed_eviction,
	.need_more_eviction=NULL,
	.update_eviction_hint=fine_update_eviction_hint,
	.is_hit_eviction=NULL,
	.update_hit_eviction_hint=NULL,
	.is_eviction_hint_full=fine_is_eviction_hint_full,
	.get_remain_space=fine_get_remain_space,
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
	.dump_cache_update=fine_dump_cache_update,
	.load_specialized_meta=NULL,
	.update_dynamic_size=NULL,
	.empty_cache=fine_empty_cache,
	.exist=fine_exist,
	.print_log=fine_print_log,
};

uint32_t entry_to_lba(void *_entry){
	fine_cache *fc=(fine_cache*)_entry;
	return fc->lba;
}

extern uint32_t test_ppa;
static inline void fine_mapping_sanity_checker(char *value){
	uint32_t *map=(uint32_t*)value;
	for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
		if(map[i]!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm, map[i])){
			uint32_t lba=((uint32_t*)demand_ftl.bm->get_oob(demand_ftl.bm, map[i]/4))[map[i]%4];
			
			if(fine_exist(NULL,lba)){
				continue;
			}
			printf("%u %u mapping sanity error\n", lba, map[i]);
			abort();
		}
	}
}

uint32_t fine_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&fcm.lru, NULL, entry_to_lba);
	fcm.max_caching_map=(total_caching_physical_pages*PAGESIZE/FINECACHEENT_SZ);
	fcm.max_caching_map-=(fcm.max_caching_map/8)/FINECACHEENT_SZ; //for dirty bit
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
	fine_print_log(mc);
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

static inline void map_size_check(uint32_t *eviction_hint){
	if(fcm.max_caching_map < fcm.now_caching_map+(*eviction_hint)){
		//printf("[mc]%u > %u+%u-%u=%u\n", fcm.max_caching_map, fcm.now_caching_map, (*eviction_hint), fcm.now_evicting_map,fcm.now_caching_map+(*eviction_hint)-fcm.now_evicting_map, fcm.now_caching_map+(*eviction_hint));
	//	printf("now caching map bigger!!!! %u %s:%d\n",fcm.now_caching_map+(*eviction_hint)-fcm.max_caching_map, __FILE__, __LINE__);
	//	abort();
	}
	//printf("[mc]%u > %u+%u-%u=%u, %u\n", fcm.max_caching_map, fcm.now_caching_map, (*eviction_hint), fcm.now_evicting_map,fcm.now_caching_map+(*eviction_hint)-fcm.now_evicting_map, fcm.now_caching_map+(*eviction_hint));
}

uint32_t fine_is_needed_eviction(struct my_cache *mc, uint32_t , uint32_t *, uint32_t eviction_hint){
//	printf("[ne]%u > %u+%u-%u=%u, %u\n", fcm.max_caching_map, fcm.now_caching_map, (eviction_hint), fcm.now_evicting_map,fcm.now_caching_map+(eviction_hint)-fcm.now_evicting_map, fcm.now_caching_map+(eviction_hint));
	if(eviction_hint < fcm.now_evicting_map){
	//	printf("break!\n");
	}
	if(fcm.max_caching_map <=fcm.now_caching_map+ (eviction_hint)/*-fcm.now_evicting_map*/) 
		return fcm.now_caching_map?NORMAL_EVICTION:EMPTY_EVICTION;
	//map_size_check(&eviction_hint);
	return HAVE_SPACE;
}

uint32_t fine_update_eviction_hint(struct my_cache *, uint32_t lba, uint32_t *prefetching_info, uint32_t eviction_hint, 
		uint32_t *now_eviction_hint, bool increase){
	if(increase){
		*now_eviction_hint=1;
		return eviction_hint+*now_eviction_hint;
	}else{
		return eviction_hint-*now_eviction_hint;
	}
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
	return (fine_cache*)lru_find(fcm.lru, lba);
	/*
	lru_node *ln;
	fine_cache *fc;
	for_each_lru_list(fcm.lru, ln){
		fc=(fine_cache*)ln->data;
		if(fc->lba==lba) return fc;
	}*/
#endif
	return NULL;
}


void fine_print_log(struct my_cache*){
	bool mapcheck[RANGE/(PAGESIZE/sizeof(uint32_t))]={0,};
	lru_node *target;
	for_each_lru_list(fcm.lru, target){
		fine_cache *fc=(fine_cache*)target->data;
		mapcheck[fc->lba/(PAGESIZE/sizeof(uint32_t))]=true;
	}

	uint32_t cnt=0;
	for(uint32_t i=0; i<RANGE/(PAGESIZE/sizeof(uint32_t)); i++){
		if(mapcheck[i]){
			cnt++;
		}
	}

	printf("finecache_log trans_page num, ratio, entry_cnt: %u (%.2lf), %u\n", cnt, (float)cnt/(RANGE/(PAGESIZE/sizeof(uint32_t))), fcm.lru->size);
}

static inline uint32_t __update_entry(GTD_entry *etr,uint32_t lba, uint32_t ppa, bool isgc, uint32_t *eviction_hint){
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
		if(eviction_hint){
			map_size_check(eviction_hint);
		}
		else if(fcm.now_caching_map > fcm.max_caching_map){
			printf("caching overflow! %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		goto last;
	}

	old_ppa=fc->ppa;
	fc->ppa=ppa;
	
	if(lba==debug_lba){
		printf("target update in cache %u,%u\n", lba, ppa);
	}
last:
	set_flag(fc, DIRTY_FLAG);
	if(!isgc){
		lru_update(fcm.lru, get_ln(fc));
	}
	return old_ppa;
}

uint32_t fine_update_entry(struct my_cache *mc, GTD_entry *e, uint32_t lba, uint32_t ppa, uint32_t *eviction_hint){
	return __update_entry(e, lba, ppa, false, eviction_hint);
}

uint32_t fine_update_entry_gc(struct my_cache *mc, GTD_entry *e, uint32_t lba, uint32_t ppa){
	return __update_entry(e, lba, ppa, true, NULL);
}

uint32_t fine_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t lba, char *data, 
		uint32_t *eviction_hint, uint32_t org_eviction_hint){
	if(etr->status==EMPTY){
		printf("try to read not populated entry! %s:%d\n",__FILE__, __LINE__);
		abort();
	}

	uint32_t *map=(uint32_t*)data;
	if(lba==debug_lba){
		printf("insert %u, %u\n", lba, map[GETOFFSET(lba)]);
	}

	//fine_mapping_sanity_checker(data);


	if(bitmap_is_set(fcm.populated_cache_entry,lba)){
		(*eviction_hint)-=org_eviction_hint;
#ifdef DFTL_DEBUG
		printf("already in cache lba:%u\n", lba);
#endif
		map_size_check(eviction_hint);
		return 1;
	}

	fine_cache *fc=(fine_cache*)malloc(sizeof(fine_cache));
	fc->lba=lba;
	fc->ppa=map[GETOFFSET(lba)];
	if(fc->ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm,fc->ppa)){
		printf("invalidated lba:%u ppa:%u\n", fc->lba, fc->ppa);
		abort();
	}

	init_fcn(fc);
#ifdef SEARCHSPEEDUP
	fcm.cl_mapping->fc_array[lba]=(void*)fc;
#endif
	bitmap_set(fcm.populated_cache_entry, lba);

	//printf("inserted lba:%u\n", lba);
	get_ln(fc)=lru_push(fcm.lru, (void*)fc);
	set_flag(fc,CLEAN_FLAG);
	(*eviction_hint)-=org_eviction_hint;
	fcm.now_caching_map++;
	map_size_check(eviction_hint);
	return 0;
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

mapping_entry *fine_get_eviction_entry(struct my_cache *, uint32_t lba, uint32_t, void **, bool *all_entry_evicting){
	lru_node *target;
	*all_entry_evicting=false;
	//checking_lba_exist(1778630);
	for_each_lru_backword(fcm.lru, target){
		fine_cache *fc=(fine_cache*)target->data;
	//	printf("eviction lba:%u ppa:%u gtdidx:%u dirty:%u\n", fc->lba, fc->ppa, GETGTDIDX(fc->lba), get_flag(fc));
		if(get_flag(fc)==EVICTING_FLAG) continue;

		if(get_flag(fc)==CLEAN_FLAG){
			if(fc && fc->ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm,fc->ppa)){
				printf("eviction invalidated lba:%u ppa:%u\n", fc->lba, fc->ppa);
				abort();
			}
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
			if(fc && fc->ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm,fc->ppa)){
				printf("eviction invalidated lba:%u ppa:%u\n", fc->lba, fc->ppa);
				abort();
			}
			set_flag(fc, EVICTING_FLAG);	
			fcm.now_evicting_map++;
	//		printf("lba:%u target_lba:%u\n", lba, fc->lba);
			if(lba==2804820){
				printf("break!\n");
			}
			/*
			lru_delete(fcm.lru, get_ln(fc));
#ifdef SEARCHSPEEDUP
			fcm.cl_mapping->fc_array[fc->lba]=NULL;
#endif
			bitmap_unset(fcm.populated_cache_entry, fc->lba);
			fcm.now_caching_map--;
			 */
			return fc;
		}
	}
	*all_entry_evicting=true;
	return NULL;
}

bool fine_update_eviction_target_translation(struct my_cache* ,uint32_t,  GTD_entry *etr, mapping_entry *map, char *data, void *, bool batch_update){
	uint32_t gtd_idx=GETGTDIDX(map->lba);
	if(fcm.GTD_internal_state[gtd_idx]==0){
		fcm.GTD_internal_state[gtd_idx]=1;
		memset(data, -1, PAGESIZE);
	}
	uint32_t *ppa_list=(uint32_t*)data;
	fine_cache *fc;
	bool debug_flag=false;

	uint32_t old_ppa;


	if(etr->idx==GETGTDIDX(debug_lba)){
		printf("target tr_page evicting [prev]: %u,%u\n", debug_lba, ((uint32_t*)data)[GETOFFSET(debug_lba)]);
	}

	if(batch_update){
		bool find_target_entry=false;
#ifdef SEARCHSPEEDUP
		uint32_t unit=PAGESIZE/sizeof(DMF);
		for(uint32_t i=0; i<unit; i++){
			fc=__find_lru_map(gtd_idx*unit+i);
			if(fc && fc->ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm,fc->ppa)){
				printf("update invalidated lba:%u ppa:%u\n", fc->lba, fc->ppa);
				abort();
			}
			if(!fc || get_flag(fc)==CLEAN_FLAG) continue;
			if(get_flag(fc)==EVICTING_FLAG && fc->lba==map->lba){
				fcm.now_evicting_map--;
				//printf("remove target_lba:%u\n", fc->lba);
				if(fcm.now_evicting_map<0){
					EPRINT("fcm.now_evicting_map should be natuer number", true);
				}
				set_flag(fc,CLEAN_FLAG);
			}

			if(get_flag(fc)==EVICTING_FLAG){

			}
			else{
				set_flag(fc,CLEAN_FLAG);
			}

#ifdef DFTL_DEBUG
			printf("%u %u dirty update\n",fc->lba, fc->ppa);
#endif
			old_ppa=ppa_list[GETOFFSET(fc->lba)];
			ppa_list[GETOFFSET(fc->lba)]=fc->ppa;
		}
#else
		//for_each_lru_list(fcm.lru, target){
		for(uint32_t i=0; i<PAGESIZE/sizeof(DMF);i++){
			fc=(fine_cache*)lru_find(fcm.lru, gtd_idx*PAGESIZE/sizeof(DMF)+i);
			if(!fc) continue;
			if(fc && fc->ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm,fc->ppa)){
				printf("update invalidated lba:%u ppa:%u\n", fc->lba, fc->ppa);
				abort();
			}
			if(GETGTDIDX(fc->lba)!=gtd_idx) continue;
			if(get_flag(fc)==CLEAN_FLAG) continue;
			if(get_flag(fc)==EVICTING_FLAG && fc->lba==map->lba){
				fcm.now_evicting_map--;
	//			printf("remove target_lba:%u\n", fc->lba);
				if(fcm.now_evicting_map<0){
					EPRINT("fcm.now_evicting_map should be natuer number", true);
				}
				set_flag(fc,CLEAN_FLAG);
			}

			set_flag(fc,CLEAN_FLAG);

#ifdef DFTL_DEBUG
			printf("%u %u dirty update\n",fc->lba, fc->ppa);
#endif
			old_ppa=ppa_list[GETOFFSET(fc->lba)];
			ppa_list[GETOFFSET(fc->lba)]=fc->ppa;
		}
#endif
	}
	else{
		set_flag((fine_cache*)map,CLEAN_FLAG);
	}

	if(etr->idx==GETGTDIDX(debug_lba)){
		printf("target tr_page evicting [post]: %u,%u\n", debug_lba, ((uint32_t*)data)[GETOFFSET(debug_lba)]);
	}
	ppa_list[GETOFFSET(map->lba)]=map->ppa;
	if(map->ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm,map->ppa)){
		printf("update invalidated lba:%u ppa:%u\n", fc->lba, fc->ppa);
		abort();
	}

	fine_evict_target(NULL, NULL, map);
	return  true;
}


bool fine_evict_target(struct my_cache *, GTD_entry *, mapping_entry *fc){
	if(!get_ln(fc)){
		EPRINT("it must have lru_node", true);
	}
	if(get_flag(fc)==DIRTY_FLAG){
		EPRINT("it was updated in updating mapping logic", true);
	}

	if(get_flag(fc)==EVICTING_FLAG){
		fcm.now_evicting_map--;
//		printf("remove target_lba:%u\n", fc->lba);
		if(fcm.now_evicting_map<0){
			EPRINT("fcm.now_evicting_map should be natuer number", true);
		}
		set_flag(fc,CLEAN_FLAG);	
	}

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
#ifdef SEARCHSPEEDUP
		if(!fcm.cl_mapping->fc_array[lba]){
			printf("bitmap is wiered! %u\n",lba);
			abort();
		}
#endif
	}
	return res;
}

void fine_force_put_mru(struct my_cache *, GTD_entry *,mapping_entry *map,  uint32_t lba){
	lru_update(fcm.lru, get_ln(map));
}

bool fine_is_eviction_hint_full(struct my_cache *, uint32_t eviction_hint){
	return fcm.max_caching_map==eviction_hint;
}

int32_t fine_get_remain_space(struct my_cache *, uint32_t total_eviction_hint){
	return fcm.max_caching_map-fcm.now_caching_map-total_eviction_hint;
}

bool fine_dump_cache_update(struct my_cache *, GTD_entry *etr, char *data){
	uint32_t gtd_idx=etr->idx;
	if(fcm.GTD_internal_state[gtd_idx]==0){
		fcm.GTD_internal_state[gtd_idx]=1;
		memset(data, -1, PAGESIZE);
	}
	fine_cache *fc;
	uint32_t unit=PAGESIZE/sizeof(DMF);
	uint32_t changed=0;
	uint32_t *ppa_list=(uint32_t*)data;
	for(uint32_t i=0; i<unit; i++){
		fc=__find_lru_map(gtd_idx*unit + i);
		if(!fc) continue;
		if(get_flag(fc)==DIRTY_FLAG){
			changed++;
			uint32_t old_ppa=ppa_list[GETOFFSET(fc->lba)];
			//invalidate ppa
			//invalidate_ppa(old_ppa);//it would be invalidated early function.
			ppa_list[GETOFFSET(fc->lba)]=fc->ppa;
		}
	}
	return changed?1:0;
}

void fine_empty_cache(struct my_cache* mc){
	while(1){
		fine_cache *fc=(fine_cache*)lru_pop(fcm.lru);
		if(!fc) break;
		bitmap_unset(fcm.populated_cache_entry, fc->lba);
		free(fc->private_data);
		free(fc);
	}
	fcm.now_caching_map=0;
}