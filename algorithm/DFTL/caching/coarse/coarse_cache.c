#include "coarse_cache.h"
#include "../../demand_mapping.h"
#include "../../../../include/settings.h"
#include "../../../../include/utils/data_copy.h"
#include <stdio.h>
#include <stdlib.h>

extern uint32_t test_key;

my_cache coarse_cache_func{
	.init=coarse_init,
	.free=coarse_free,
	.is_needed_eviction=coarse_is_needed_eviction,
	.need_more_eviction=NULL,
	.update_eviction_hint=coarse_update_eviction_hint,
	.is_hit_eviction=NULL,
	.update_hit_eviction_hint=NULL,
	.is_eviction_hint_full=coarse_is_eviction_hint_full,
	.get_remain_space=coarse_get_remain_space,
	.update_entry=coarse_update_entry,
	.update_entry_gc=coarse_update_entry_gc,
	.force_put_mru=coarse_force_put_mru,
	.insert_entry_from_translation=coarse_insert_entry_from_translation,
	.update_from_translation_gc=coarse_update_from_translation_gc,
	.get_mapping=coarse_get_mapping,
	.get_eviction_GTD_entry=coarse_get_eviction_GTD_entry,
	.get_eviction_mapping_entry=NULL,
	.update_eviction_target_translation=coarse_update_eviction_target_translation,
	.evict_target=NULL,
	.dump_cache_update=coarse_dump_cache_update,
	.load_specialized_meta=NULL,
	.update_dynamic_size=NULL,
	.empty_cache=coarse_empty_cache,
	.exist=coarse_exist,
	.print_log=NULL,
};

coarse_cache_monitor ccm;
extern demand_map_manager dmm;
#ifdef NO_MEMCPY_DATA
static uint32_t *exact_mapping;
#endif

uint32_t coarse_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&ccm.lru, NULL, NULL);
	uint32_t max_translate_page_num=RANGE/(PAGESIZE/sizeof(uint32_t));
	uint64_t total_memory=total_caching_physical_pages*PAGESIZE - max_translate_page_num/8;
	ccm.max_caching_page=total_memory/(PAGESIZE+16);
	ccm.now_caching_page=0;
	mc->type=COARSE;
	mc->private_data=NULL;
	
#ifdef NO_MEMCPY_DATA
	exact_mapping=(uint32_t*)malloc(sizeof(uint32_t)*RANGE);
	memset(exact_mapping, -1, sizeof(uint32_t)*RANGE);
#endif

	return ccm.max_caching_page * (PAGESIZE/sizeof(uint32_t));
}

uint32_t coarse_free(struct my_cache *mc){
	while(1){
		coarse_cache *cc=(coarse_cache*)lru_pop(ccm.lru);
		if(!cc) break;
		free(cc->data);
		free(cc);
	}
	lru_free(ccm.lru);
	return 1;
}

uint32_t coarse_is_needed_eviction(struct my_cache *mc, uint32_t , uint32_t *, uint32_t eviction_hint){
	if(ccm.max_caching_page == ccm.now_caching_page+ (eviction_hint)){
		return ccm.now_caching_page?NORMAL_EVICTION:EMPTY_EVICTION;
	}

	if(ccm.max_caching_page<ccm.now_caching_page+(eviction_hint)){
		printf("now caching page bigger!!!! %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	return HAVE_SPACE;
}


uint32_t coarse_update_eviction_hint(struct my_cache *, uint32_t lba, uint32_t * /*prefetching_info*/, uint32_t eviction_hint, 
		uint32_t *now_eviction_hint, bool increase){
	if(increase){
		*now_eviction_hint=1;
		return eviction_hint+(*now_eviction_hint);
	}else{
		return eviction_hint-(*now_eviction_hint);
	}
}

inline static void check_caching_size(uint32_t eviction_hint){
	static int cnt=0;

	if((int)eviction_hint<0){
		printf("minus ?????\n");
		abort();
	}

	//printf("cc - now:%u\n", ccm.now_caching_page);
	if(ccm.now_caching_page + eviction_hint> ccm.max_caching_page){
		printf("caching overflow! %s:%d\n", __FILE__, __LINE__);
		abort();
	}
}

inline static uint32_t __update_entry(GTD_entry *etr, uint32_t lba, uint32_t ppa, bool isgc, uint32_t *eviction_hint){
	coarse_cache *cc;
	uint32_t old_ppa;
	lru_node *ln;
	if(etr->status==EMPTY){
		cc=(coarse_cache*)malloc(sizeof(coarse_cache));
		cc->data=(char*)malloc(PAGESIZE);
		data_set(cc->data, -1, PAGESIZE);
		cc->etr=etr;
		ln=lru_push(ccm.lru, cc);
		etr->private_data=(void*)ln;
		ccm.now_caching_page++;
	}else{
		if(etr->private_data==NULL){
			printf("insert translation page before cache update! %s:%d\n",__FILE__, __LINE__);
			abort();
		}
		ln=(lru_node*)etr->private_data;
		cc=(coarse_cache*)(ln->data);
	}

#ifdef NO_MEMCPY_DATA
	old_ppa=exact_mapping[lba];
	exact_mapping[lba]=ppa;
#else
	uint32_t *ppa_list=(uint32_t*)cc->data;
	old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
#endif
/*
	if(lba==test_key)
		printf("%u ppa change %u to %u\n",test_key, old_ppa, ppa);
	}
*/
	if(!isgc){
		lru_update(ccm.lru, ln);
	}
	etr->status=DIRTY;
	if(eviction_hint){
		check_caching_size(*eviction_hint);
	}
	else{	
		check_caching_size(0);
	}
	return old_ppa;
}

uint32_t coarse_update_entry(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa, uint32_t *eviction_hint){
	return __update_entry(etr, lba, ppa, false, eviction_hint);
}

uint32_t coarse_update_entry_gc(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	return __update_entry(etr, lba, ppa, true, NULL);
}

uint32_t coarse_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t lba, char *data, uint32_t *eviction_hint, uint32_t now_eviction_hint){
	if(etr->private_data){
		printf("already lru node exists! %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	coarse_cache *cc=(coarse_cache*)malloc(sizeof(coarse_cache));
	cc->data=(char*)malloc(PAGESIZE);
	data_copy(cc->data, data, PAGESIZE);
	cc->etr=etr;
	etr->private_data=(void *)lru_push(ccm.lru, (void*)cc);
	etr->status=CLEAN;
	ccm.now_caching_page++;
	(*eviction_hint)-=now_eviction_hint;
	check_caching_size(*eviction_hint);
	return 1;
}

uint32_t coarse_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa){
	uint32_t *ppa_list=(uint32_t*)data;
#ifdef NO_MEMCPY_DATA
	uint32_t old_ppa=exact_mapping[lba];
	exact_mapping[lba]=ppa;
#else
	uint32_t old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
#endif
	return old_ppa;
}

uint32_t coarse_get_mapping(struct my_cache *, uint32_t lba){
	uint32_t gtd_idx=GETGTDIDX(lba);
	GTD_entry *etr=&dmm.GTD[gtd_idx];
	if(!etr->private_data){
		printf("%u insert data before pick mapping! %s:%d\n",lba, __FILE__, __LINE__);
		abort();
	}

#ifdef NO_MEMCPY_DATA
	return exact_mapping[lba];
#else
	uint32_t *ppa_list=(uint32_t*)DATAFROMLN((lru_node*)etr->private_data);
	return ppa_list[GETOFFSET(lba)];
#endif
}

struct GTD_entry *coarse_get_eviction_GTD_entry(struct my_cache *, uint32_t lba){
	lru_node *target;
	GTD_entry *etr=NULL;
	for_each_lru_backword(ccm.lru, target){
		coarse_cache *cc=(coarse_cache*)target->data;
		etr=cc->etr;
		if(etr->status==FLYING || etr->status==EVICTING){
			continue;
		}
		if(etr->status==CLEAN){
			etr->private_data=NULL;
			cc=(coarse_cache*)target->data;
			free(cc->data);
			free(target->data);
			lru_delete(ccm.lru, target);
			ccm.now_caching_page--;
			return NULL;
		}

		if(etr->status!=DIRTY){
			printf("can't be status %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		return etr;
	}
	return NULL;
}

bool coarse_update_eviction_target_translation(struct my_cache* , uint32_t, GTD_entry *etr,mapping_entry *map, char *data, void *, bool){
	char *c_data=(char*)DATAFROMLN((lru_node*)etr->private_data);
#ifdef NO_MEMCPY_DATA
#else
	memcpy(data, c_data, PAGESIZE);
#endif
	free(c_data);
	free(((lru_node*)etr->private_data)->data);
	lru_delete(ccm.lru, (lru_node*)etr->private_data);
	etr->private_data=NULL;
	ccm.now_caching_page--;
	return true;
}

bool coarse_exist(struct my_cache *, uint32_t lba){
	return dmm.GTD[GETGTDIDX(lba)].private_data!=NULL;
}

void coarse_force_put_mru(struct my_cache*, struct GTD_entry *etr, mapping_entry *m, uint32_t lba){
	lru_update(ccm.lru, (lru_node*)etr->private_data);
}

bool coarse_is_eviction_hint_full(struct my_cache *, uint32_t eviction_hint){
	return eviction_hint==ccm.max_caching_page;
}

int32_t coarse_get_remain_space(struct my_cache *, uint32_t total_eviction_hint){
	return ccm.max_caching_page-ccm.now_caching_page-total_eviction_hint;
}

bool coarse_dump_cache_update(struct my_cache *,GTD_entry *etr, char *data){
	if(!etr->private_data){
		return false;
	}
	char *c_data=(char*)DATAFROMLN((lru_node*)etr->private_data);
	memcpy(data, c_data, PAGESIZE);
	return true;
}

void coarse_empty_cache(struct my_cache *mc){
	while(1){
		coarse_cache *cc=(coarse_cache*)lru_pop(ccm.lru);
		if(!cc) break;
		cc->etr->private_data=NULL;
		free(cc->data);
		free(cc);
	}

	ccm.now_caching_page=0;
}