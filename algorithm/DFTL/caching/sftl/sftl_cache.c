#include "sftl_cache.h"
#include "bitmap_ops.h"
#include "../../demand_mapping.h"
#include "../../../../include/settings.h"
#include <stdio.h>
#include <stdlib.h>

extern uint32_t test_key;

my_cache sftl_cache_func{
	.init=sftl_init,
	.free=sftl_free,
	.is_needed_eviction=sftl_is_needed_eviction,
	.need_more_eviction=sftl_need_more_eviction,
	.update_entry=sftl_update_entry,
	.update_entry_gc=sftl_update_entry_gc,
	.insert_entry_from_translation=sftl_insert_entry_from_translation,
	.update_from_translation_gc=sftl_update_from_translation_gc,
	.get_mapping=sftl_get_mapping,
	.get_eviction_GTD_entry=sftl_get_eviction_GTD_entry,
	.get_eviction_mapping_entry=NULL,
	.update_eviction_target_translation=sftl_update_eviction_target_translation,
	.evict_target=NULL,
	.exist=sftl_exist,
};

sftl_cache_monitor scm;
extern demand_map_manager dmm;

uint32_t sftl_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&scm.lru, NULL);
	scm.max_caching_byte=total_caching_physical_pages * PAGESIZE;
	scm.now_caching_byte=0;
	mc->type=COARSE;
	mc->entry_type=DYNAMIC;
	mc->private_data=NULL;
	scm.gtd_size=(uint32_t*)malloc(GTDNUM *sizeof(uint32_t));
	for(uint32_t i=0; i<scm.gtd_size; i++){
		scm.gtd_size[i]=BITMAPSIZE;
	}

	printf("|\tcaching <min> percentage: %.2lf%%\n", (double) ((scm.max_caching_byte/(BITMAPSIZE+PAGESIZE)) * BITMAPMEMBER)/ RANGE *100);
	return (scm.max_caching_byte/(BITMAPSIZE+sizeof(uint32_t))) * BITMAPMEMBER;
}

uint32_t sftl_free(struct my_cache *mc){
	while(1){
		sftl_cache *cc=(coarse_cache*)lru_pop(ccm.lru);
		if(!cc) break;
		free(cc->head_array);
		bitmap_free(cc->map);
		free(acc);
	}
	lru_free(scm.lru);
	free(scm.gtd_size);
	return 1;
}

bool sftl_is_needed_eviction(struct my_cache *mc, uint32_t lba){
	uint32_t target_size=scm.gtd_size[GETGTDIDX(lba)];
	if(ccm.max_caching_byte>= ccm.now_caching_byte+target_size){
		return true;
	}
	if(ccm.max_caching_byte>=ccm.now_caching_byte){
		printf("now caching byte bigger!!!! %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	return false;
}

inline static sftl_cache* get_initial_state_cache(uint32_t gtd_idx, GTD_entry *etr){
	sftl_cache *res=(sftl_cache *)malloc(sizeof(sftl_cache));
	res->head_array=(uint32_t*)malloc(PAGESIZE);
	memset(head_array, -1, PAGESIZE);
	res->bitmap=bitamp_set_init(BITMAPMEMBER);
	res->etr=etr;
	
	scm.gtd_size[gtd_idx]=PAGESIZE+BITMAPSIZE;
	
	return res;
}

inline static uint32_t __update_entry(GTD_entry *etr, uint32_t lba, uint32_t ppa, bool isgc){
	sftl_cache *sc;
	uint32_t old_ppa;
	lru_node *ln;
	if(etr->status==EMPTY){
	
	}else{
		if(etr->private_data==NULL){
			printf("insert translation page before cache update! %s:%d\n",__FILE__, __LINE__);
			abort();
		}
		ln=(lru_node*)etr->private_data;
		cc=(sftl_cache*)(ln->data);
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
		lru_update(scm.lru, ln);
	}
	etr->status=DIRTY;
	return old_ppa;
}

uint32_t sftl_update_entry(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	return __update_entry(etr, lba, ppa, false);
}

uint32_t sftl_update_entry_gc(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	return __update_entry(etr, lba, ppa, true);
}

uint32_t sftl_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t lba, char *data){
	if(etr->private_data){
		printf("already lru node exists! %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	sftl_cache *cc=(coarse_cache*)malloc(sizeof(coarse_cache));
	cc->data=(char*)malloc(PAGESIZE);
	memcpy(cc->data, data, PAGESIZE);
	cc->etr=etr;
	etr->private_data=(void *)lru_push(ccm.lru, (void*)cc);
	etr->status=CLEAN;
	ccm.now_caching_page++;
	return 1;
}

uint32_t sftl_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa){
	uint32_t *ppa_list=(uint32_t*)data;
	uint32_t old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
	return old_ppa;
}

uint32_t sftl_get_mapping(struct my_cache *, uint32_t lba){
	uint32_t gtd_idx=GETGTDIDX(lba);
	GTD_entry *etr=&dmm.GTD[gtd_idx];
	if(!etr->private_data){
		printf("insert data before pick mapping! %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	uint32_t *ppa_list=(uint32_t*)DATAFROMLN((lru_node*)etr->private_data);
	return ppa_list[GETOFFSET(lba)];
}

struct GTD_entry *sftl_get_eviction_GTD_entry(struct my_cache *){
	lru_node *target;
	GTD_entry *etr=NULL;
	for_each_lru_backword(ccm.lru, target){
		sftl_cache *cc=(coarse_cache*)target->data;
		etr=cc->etr;
		if(etr->status==FLYING || etr->status==EVICTING){
			continue;
		}
		if(etr->status==CLEAN){
			etr->private_data=NULL;
			cc=(sftl_cache*)target->data;
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

bool sftl_update_eviction_target_translation(struct my_cache* , GTD_entry *etr,mapping_entry *map, char *data){
	char *c_data=(char*)DATAFROMLN((lru_node*)etr->private_data);
	memcpy(data, c_data, PAGESIZE);
	free(c_data);
	free(((lru_node*)etr->private_data)->data);
	lru_delete(ccm.lru, (lru_node*)etr->private_data);
	etr->private_data=NULL;
	ccm.now_caching_page--;
	return true;
}

bool sftl_exist(struct my_cache *, uint32_t lba){
	return dmm.GTD[GETGTDIDX(lba)].private_data!=NULL;
}
