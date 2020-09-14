#include "coarse_cache.h"
#include "../../demand_mapping.h"
#include "../../../../include/settings.h"
#include <stdio.h>
#include <stdlib.h>

extern uint32_t test_key;

my_cache coarse_cache_func{
	.init=coarse_init,
	.free=coarse_free,
	.is_needed_eviction=coarse_is_needed_eviction,
	.need_more_eviction=NULL,
	.update_entry=coarse_update_entry,
	.update_entry_gc=coarse_update_entry_gc,
	.insert_entry_from_translation=coarse_insert_entry_from_translation,
	.update_from_translation_gc=coarse_update_from_translation_gc,
	.get_mapping=coarse_get_mapping,
	.get_eviction_GTD_entry=coarse_get_eviction_GTD_entry,
	.get_eviction_mapping_entry=NULL,
	.update_eviction_target_translation=coarse_update_eviction_target_translation,
	.evict_target=NULL,
	.update_dynamic_size=NULL,
	.exist=coarse_exist,
};

coarse_cache_monitor ccm;
extern demand_map_manager dmm;

uint32_t coarse_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&ccm.lru, NULL);
	ccm.max_caching_page=total_caching_physical_pages;
	ccm.now_caching_page=0;
	mc->type=COARSE;
	mc->private_data=NULL;

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

bool coarse_is_needed_eviction(struct my_cache *mc, uint32_t ){
	if(ccm.max_caching_page==ccm.now_caching_page){
		return true;
	}
	if(ccm.max_caching_page<ccm.now_caching_page){
		printf("now caching page bigger!!!! %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	return false;
}

inline static uint32_t __update_entry(GTD_entry *etr, uint32_t lba, uint32_t ppa, bool isgc){
	coarse_cache *cc;
	uint32_t old_ppa;
	lru_node *ln;
	if(etr->status==EMPTY){
		cc=(coarse_cache*)malloc(sizeof(coarse_cache));
		cc->data=(char*)malloc(PAGESIZE);
		memset(cc->data, -1, PAGESIZE);
		cc->etr=etr;
		ln=lru_push(ccm.lru, cc);
		etr->private_data=(void*)ln;
		ccm.now_caching_page++;
		if(ccm.now_caching_page > ccm.max_caching_page){
			printf("caching overflow! %s:%d\n", __FILE__, __LINE__);
			abort();
		}
	}else{
		if(etr->private_data==NULL){
			printf("insert translation page before cache update! %s:%d\n",__FILE__, __LINE__);
			abort();
		}
		ln=(lru_node*)etr->private_data;
		cc=(coarse_cache*)(ln->data);
	}

	uint32_t *ppa_list=(uint32_t*)cc->data;
	old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
/*
	if(lba==test_key){
		printf("%u ppa change %u to %u\n",test_key, old_ppa, ppa);
	}
*/
	if(!isgc){
		lru_update(ccm.lru, ln);
	}
	etr->status=DIRTY;
	return old_ppa;
}

uint32_t coarse_update_entry(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	return __update_entry(etr, lba, ppa, false);
}

uint32_t coarse_update_entry_gc(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	return __update_entry(etr, lba, ppa, true);
}

uint32_t coarse_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t lba, char *data){
	if(etr->private_data){
		printf("already lru node exists! %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	coarse_cache *cc=(coarse_cache*)malloc(sizeof(coarse_cache));
	cc->data=(char*)malloc(PAGESIZE);
	memcpy(cc->data, data, PAGESIZE);
	cc->etr=etr;
	etr->private_data=(void *)lru_push(ccm.lru, (void*)cc);
	etr->status=CLEAN;
	ccm.now_caching_page++;
	return 1;
}

uint32_t coarse_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa){
	uint32_t *ppa_list=(uint32_t*)data;
	uint32_t old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
	return old_ppa;
}

uint32_t coarse_get_mapping(struct my_cache *, uint32_t lba){
	uint32_t gtd_idx=GETGTDIDX(lba);
	GTD_entry *etr=&dmm.GTD[gtd_idx];
	if(!etr->private_data){
		printf("insert data before pick mapping! %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	uint32_t *ppa_list=(uint32_t*)DATAFROMLN((lru_node*)etr->private_data);
	return ppa_list[GETOFFSET(lba)];
}

struct GTD_entry *coarse_get_eviction_GTD_entry(struct my_cache *){
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

bool coarse_update_eviction_target_translation(struct my_cache* , GTD_entry *etr,mapping_entry *map, char *data){
	char *c_data=(char*)DATAFROMLN((lru_node*)etr->private_data);
	memcpy(data, c_data, PAGESIZE);
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
