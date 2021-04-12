#include "tpftl_cache.h"
#include "../../demand_mapping.h"
#include "../../gc.h"

my_cache tp_cache_func{
	.init=tp_init,
	.free=tp_free,
	.is_needed_eviction=tp_is_needed_eviction,
	.need_more_eviction=tp_is_needed_eviction,
	.is_hit_eviction=tp_is_hit_eviction,
	.update_entry=tp_update_entry,
	.update_entry_gc=tp_update_entry_gc,
	.force_put_mru=tp_force_put_mru,
	.insert_entry_from_translation=tp_insert_entry_from_translation,
	.update_from_translation_gc=tp_update_from_translation_gc,
	.get_mapping=tp_get_mapping,
	.get_eviction_GTD_entry=NULL,
	.get_eviction_mapping_entry=tp_get_eviction_entry,
	.update_eviction_target_translation=tp_update_eviction_target_translation,
	.evict_target=tp_evict_target, 
	.update_dynamic_size=tp_update_dynamic_size,
	.exist=tp_exist,
};

extern uint32_t test_key;
extern uint32_t test_ppa;
extern demand_map_manager dmm;
tp_cache_monitor tcm;

static uint32_t TP_ENTRY_SZ;
#define TP_NODE_SZ (sizeof(tp_node)-sizeof(uint32_t))

inline static uint32_t get_bits_from_int(uint32_t t){
	uint32_t cnt=1;
	uint32_t lt=2;
	while(lt < t){
		lt*=2;
		cnt++;
	}
	return cnt;
}

inline static void etr_sanity_check(GTD_entry *etr){
	return;
	if(!etr || !etr->private_data) return;
	tp_node *tn=(tp_node*)etr->private_data;
	if(tn->idx!=etr->idx){
		printf("sanity failed!!\n");
	}
}

inline static void tc_lru_traverse(LRU *lru, tp_node *tn){
	tp_cache_node *tc;
	lru_node *ln, *lp;
	uint32_t idx=0;
	for_each_lru_backword_safe(lru, ln, lp){
		tc=(tp_cache_node*)ln->data;
		printf("[%u] lba:%u-(offset:%u) ppa:%u addr:%p dirty_bit:%d\n", 
				idx++, GETLBA(tn, tc),tc->offset, tc->ppa, tc, tc->dirty_bit);
	}
}

uint32_t tp_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&tcm.lru, NULL, NULL);
	uint32_t target_bits=(32+get_bits_from_int(PAGESIZE/sizeof(uint32_t))+1); // ppa, offset, dirty
	TP_ENTRY_SZ=target_bits/8+(target_bits%8?1:0);

	tcm.max_caching_byte=total_caching_physical_pages*PAGESIZE;
	tcm.now_caching_byte=0;

	mc->type=FINE;
	mc->entry_type=DYNAMIC;

	printf("|\tcaching <min> percentage: %.2lf%%\n", (double)(tcm.max_caching_byte/(TP_NODE_SZ+TP_ENTRY_SZ))/RANGE *100);
	tcm.populated_cache_entry=bitmap_init(RANGE);
	tcm.tp_node_change_cnt=0;
	tcm.prefetching_mode=false;
	tcm.GTD_internal_state=(char*)calloc(RANGE/(PAGESIZE/sizeof(DMF))+(RANGE%(PAGESIZE/sizeof(DMF))?1:0),sizeof(uint8_t));
	return tcm.max_caching_byte/(TP_ENTRY_SZ);
}

uint32_t tp_free(struct my_cache *mc){
	while(1){
		tp_node *tn=(tp_node*)lru_pop(tcm.lru);
		if(!tn) break;
		while(1){
			tp_cache_node *tc=(tp_cache_node*)lru_pop(tn->tp_lru);
			if(!tc) break;
			free(tc);
		}
		lru_free(tn->tp_lru);
		free(tn);
	}
	lru_free(tcm.lru);
	printf("now_caching_byte: %u max_caching_byte: %u\n", tcm.now_caching_byte, tcm.max_caching_byte);
	return 1;
}

static inline uint32_t __get_prefetching_length(tp_node *tn, uint32_t lba){
	if(!tn) return 0;

	uint32_t cnt=0;
	int32_t start_lba=(lba/(PAGESIZE/sizeof(uint32_t)))*(PAGESIZE/sizeof(uint32_t));
	for(int32_t i=lba-1; i>=start_lba; i--){
		if(bitmap_is_set(tcm.populated_cache_entry, i)){
			cnt++;
		}
	}

	if(tn->tp_lru->size-1 < cnt){
		cnt=tn->tp_lru->size-1;
	}

	if(GETOFFSET(lba)+cnt > MAXOFFSET){
		cnt=MAXOFFSET-GETOFFSET(lba);
	}
	return cnt;
}

bool tp_is_needed_eviction(struct my_cache *a, uint32_t lba, uint32_t *prefetching_num){
	GTD_entry *etr=GETETR(dmm, lba);

	uint32_t prefetching_hint=((*prefetching_num)!=UINT32_MAX?(*prefetching_num):0);
	etr_sanity_check(etr);
	uint32_t target_byte;
	if(etr->private_data){
		uint32_t cnt=__get_prefetching_length((tp_node*)etr->private_data, lba);
		target_byte=TP_ENTRY_SZ*(cnt+1);
	}
	else{
		target_byte=TP_ENTRY_SZ+TP_NODE_SZ;
	}

	if(prefetching_hint){
		for(uint32_t i=lba+1; i<=lba+prefetching_hint; i++){
			if(GETGTDIDX(i)!=GETGTDIDX(lba)) break;
			target_byte+=TP_ENTRY_SZ;
		}
	}
	
	if(tcm.max_caching_byte > tcm.now_caching_byte + target_byte){
		return false;
	}
	else return true;
}


bool tp_is_needed_more_eviction(struct my_cache *a, uint32_t lba){
	return tp_is_needed_eviction(a, lba, NULL);
}

static inline tp_node* __find_tp_node(GTD_entry *etr){
	if(!etr->private_data) return NULL;
	etr_sanity_check(etr);
	return (tp_node*)etr->private_data;
}

static inline tp_cache_node * __find_tp_cache_node(tp_node *tn, uint32_t lba){
	lru_node *ln;
	/*
	for_each_lru_list(tn->tp_lru,ln){
		tp_cache_node *tcn=(tp_cache_node*)ln->data;
		if(tcn->offset==GETOFFSET(lba)){
			return tcn;	
		}
	}*/
	return (tp_cache_node*)lru_find(tn->tp_lru, GETOFFSET(lba));
}

uint32_t tp_retrieve_key(void *_entry){
	tp_cache_node *tcn=(tp_cache_node*)_entry;
	return tcn->offset;
}

static inline uint32_t __update_entry(GTD_entry *etr, uint32_t lba, uint32_t ppa, bool isgc){
	tp_cache_node *tc;
	tp_node *tn=__find_tp_node(etr);
	uint32_t old_ppa;
	uint32_t target_byte=0;
	
	if(lba==test_key){
		printf("[%s]mapping update %u->%u\n", isgc?"gc":"normal",lba, ppa);
	}

	etr_sanity_check(etr);

	if(tn){
		if(!(tc=__find_tp_cache_node(tn, lba))){
			tc=(tp_cache_node*)malloc(sizeof(tp_cache_node));
			tc->offset=GETOFFSET(lba);
			old_ppa=UINT32_MAX;
			tc->lru_node=lru_push(tn->tp_lru, (void*)tc);
			target_byte+=TP_ENTRY_SZ;
		}
		else{
			old_ppa=tc->ppa;
		}
	}
	else{
		tn=(tp_node*)malloc(sizeof(tp_node));
		lru_init(&tn->tp_lru, NULL, tp_retrieve_key);
		tn->idx=etr->idx;

		tc=(tp_cache_node*)malloc(sizeof(tp_cache_node));
		tc->offset=GETOFFSET(lba);
		old_ppa=UINT32_MAX;

		tc->lru_node=lru_push(tn->tp_lru, (void*)tc);
		tn->lru_node=lru_push(tcm.lru, (void*)tn);

		etr->private_data=(void*)tn;
		tcm.tp_node_change_cnt++;
		if(tcm.tp_node_change_cnt > PREFETCHINGTH){
			tcm.prefetching_mode=true;
		}
		target_byte+=TP_ENTRY_SZ+TP_NODE_SZ;
	}

	
	if(tn->idx>GTDNUM){
		printf("over flow idx!!\n");
		abort();
	}

	tcm.now_caching_byte+=target_byte;

	tc->ppa=ppa;
	tc->dirty_bit=true;
	bitmap_set(tcm.populated_cache_entry, lba);

	if(!isgc){
		lru_update(tn->tp_lru, tc->lru_node);
		lru_update(tcm.lru, tn->lru_node);
	}
	return old_ppa;
}

uint32_t tp_update_entry(struct my_cache *, GTD_entry *e, uint32_t lba, uint32_t ppa){
	return __update_entry(e, lba, ppa, false);
}

uint32_t tp_update_entry_gc(struct my_cache *, GTD_entry *e, uint32_t lba, uint32_t ppa){
	return __update_entry(e, lba, ppa, true);
}

uint32_t tp_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t lba, char *data, uint32_t *prefetching_num){
	if(etr->status==EMPTY){
		printf("try to read not populated entry! %s:%d\n",__FILE__, __LINE__);
		abort();
	}
	etr_sanity_check(etr);
	tp_node *tn;
	if(etr->private_data){
		tn=(tp_node*)etr->private_data;
	}
	else{
		tn=(tp_node*)malloc(sizeof(tp_node));
		lru_init(&tn->tp_lru, NULL, tp_retrieve_key);
		tn->lru_node=lru_push(tcm.lru, (void*)tn);
		etr->private_data=(void*)tn;
		tn->idx=etr->idx;

		tcm.tp_node_change_cnt++;
		if(tcm.tp_node_change_cnt > PREFETCHINGTH){
			tcm.prefetching_mode=true;
		}
		tcm.now_caching_byte+=TP_NODE_SZ;
	}

	etr_sanity_check(etr);
	if(tn->idx>GTDNUM){
		printf("over flow idx!!\n");
		abort();
	}

	uint32_t prefetching_len=0;
	if(tcm.prefetching_mode){
		prefetching_len=__get_prefetching_length(tn,lba);
		tcm.tp_node_change_cnt=0;
		tcm.prefetching_mode=false;
	}
	prefetching_len+=*prefetching_num;

	tp_cache_node *tc;
	uint32_t *ppa_list=(uint32_t*)data;
	for(uint32_t i=0; i<1+prefetching_len; i++){
		if(GETGTDIDX(lba)!=(GETGTDIDX((lba+i)))){
			break;
		}

		if(bitmap_is_set(tcm.populated_cache_entry, lba+i)){
			continue;
		}

		tc=(tp_cache_node*)malloc(sizeof(tp_cache_node));
		tc->offset=GETOFFSET((lba+i));

		tc->ppa=ppa_list[tc->offset];
		tc->dirty_bit=false;

		if(tc->ppa==UINT32_MAX){
			free(tc); //not populated entry;
			continue;
		}

		if(i==0){
			tc->lru_node=lru_push(tn->tp_lru,(void*)tc);
		}
		else{
			tc->lru_node=lru_push_last(tn->tp_lru,(void*)tc);
		}
		
		bitmap_set(tcm.populated_cache_entry, lba+i);
		tcm.now_caching_byte+=TP_ENTRY_SZ;
	}


	return 1;
}

uint32_t tp_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa){
	uint32_t *ppa_list=(uint32_t*)data;
	uint32_t old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;
	
	GTD_entry *etr=GETETR(dmm,lba);

	etr_sanity_check(etr);
	if(etr->idx==test_key/2048 && ppa==test_ppa){
		printf("%s-", tp_exist(NULL,lba)?"true":"false");
		printf("%u gc dirty evict!\n",etr->idx);
	}
	return old_ppa;
}

uint32_t tp_get_mapping(struct my_cache *, uint32_t lba){
	tp_node *tn=__find_tp_node(GETETR(dmm, lba));
	tp_cache_node *tc=__find_tp_cache_node(tn,lba);
	lru_update(tn->tp_lru, tc->lru_node);
	lru_update(tcm.lru, tn->lru_node);
	return tc->ppa;
}

mapping_entry *tp_get_eviction_entry(struct my_cache *, uint32_t lba){
	GTD_entry *etr=GETETR(dmm,lba);
	etr_sanity_check(etr);

	tp_node *tn=(tp_node*)etr->private_data;

	if(tn && tn->idx>GTDNUM){
		printf("over flow idx!!\n");
		abort();
	}
	uint32_t target_cnt=__get_prefetching_length(tn, lba)+1;
	uint32_t remain_byte=tcm.max_caching_byte-tcm.now_caching_byte;
	uint32_t eviction_cnt=0;
	if(target_cnt==1){
		eviction_cnt=2;
	}
	else{
		eviction_cnt=target_cnt-remain_byte/TP_ENTRY_SZ;
	}

	tp_node *target_tn=(tp_node*)tcm.lru->tail->data;
	tp_cache_node *tc;
	lru_node *ln, *lp;

	if(target_tn->idx>GTDNUM){
		printf("over flow idx!!\n");
		abort();
	}
	//GTD_entry *target_etr=GETETR(dmm, target_tn->idx *(PAGESIZE/sizeof(uint32_t)));
	for_each_lru_backword_safe(target_tn->tp_lru, ln, lp){
		tc=(tp_cache_node*)ln->data;
		if(!tc->dirty_bit){
			lru_delete(target_tn->tp_lru, ln);
			bitmap_unset(tcm.populated_cache_entry, GETLBA(target_tn, tc));
			free(tc);
			tcm.now_caching_byte-=TP_ENTRY_SZ;
			if(!--eviction_cnt) break;
		}
	}



	if(!target_tn->tp_lru->size){
		lru_delete(tcm.lru, target_tn->lru_node);
		lru_free(target_tn->tp_lru);
		GTD_entry *evict_etr=&dmm.GTD[target_tn->idx];
		etr_sanity_check(evict_etr);
		free(target_tn);
		evict_etr->private_data=NULL;
	
		tcm.now_caching_byte-=TP_NODE_SZ;
		tcm.tp_node_change_cnt--;
		if(tcm.tp_node_change_cnt < -1*PREFETCHINGTH){
			tcm.prefetching_mode=true;
		}
		return NULL;
	}
	if(!eviction_cnt) return NULL;
	for_each_lru_backword_safe(target_tn->tp_lru, ln, lp){
		tc=(tp_cache_node*)ln->data;
		if(!tc->dirty_bit){
			printf("it can't has clean entry %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		else{
			mapping_entry *target=(mapping_entry*)malloc(sizeof(mapping_entry));
			target->lba=GETLBA(target_tn, tc);
			target->ppa=tc->ppa;
			/*
			if(target->lba > UINT32_MAX/2){
				printf("wtf??\n");
				abort();
			}*/
			return target;
		}
	}
	return NULL;
}

bool tp_update_eviction_target_translation(struct my_cache* , uint32_t lba, GTD_entry *etr, mapping_entry *map, char *data){
	uint32_t gtd_idx=GETGTDIDX(map->lba);
	if(tcm.GTD_internal_state[gtd_idx]==0){
		tcm.GTD_internal_state[gtd_idx]=1;
		memset(data, -1, PAGESIZE);
	}

	etr_sanity_check(etr);
	uint32_t *ppa_list=(uint32_t *)data;
	tp_node *tn=(tp_node*)etr->private_data;
	tp_cache_node *tc;

	lru_node *ln;
	uint32_t old_ppa;

	for_each_lru_list(tn->tp_lru, ln){
		tc=(tp_cache_node*)ln->data;
		if(tc->dirty_bit){
			old_ppa=ppa_list[tc->offset];
			ppa_list[tc->offset]=tc->ppa;
			/*
			if(old_ppa!=UINT32_MAX && !dmm.bm->is_invalid_page(dmm.bm, old_ppa) && old_ppa!=tc->ppa){
				invalidate_ppa(old_ppa);
			}*/
			tc->dirty_bit=0;
		}
	}

	uint32_t target_cnt=__get_prefetching_length(tn, lba)+1;
	uint32_t remain_byte=tcm.max_caching_byte-tcm.now_caching_byte;
	int32_t eviction_cnt=0;
	if(target_cnt==1){
		eviction_cnt=2;
	}
	else{
		eviction_cnt=target_cnt-remain_byte/TP_ENTRY_SZ;
	}
	
	free(map);
	if(eviction_cnt<0) return true;

	lru_node *lp;
	for_each_lru_backword_safe(tn->tp_lru, ln, lp){
		tc=(tp_cache_node*)ln->data;
		if(!tc->dirty_bit){
			lru_delete(tn->tp_lru, ln);	
			bitmap_unset(tcm.populated_cache_entry, GETLBA(tn, tc));
			free(tc);
			tcm.now_caching_byte-=TP_ENTRY_SZ;
			if(--eviction_cnt) break;
		}
	}

	if(!tn->tp_lru->size){
		lru_delete(tcm.lru, tn->lru_node);
		lru_free(tn->tp_lru);
		free(tn);

		etr->private_data=NULL;

		tcm.tp_node_change_cnt--;
		if(tcm.tp_node_change_cnt < -1*PREFETCHINGTH){
			tcm.prefetching_mode=true;
		}
		tcm.now_caching_byte-=TP_NODE_SZ;
		return NULL;
	}
	return true;
}	

bool tp_evict_target(struct my_cache *, GTD_entry *, mapping_entry *t){
	return true;
}

bool tp_exist(struct my_cache *, uint32_t lba){
	return bitmap_is_set(tcm.populated_cache_entry, lba);
}

void tp_force_put_mru(struct my_cache *, GTD_entry *etr, mapping_entry *, uint32_t lba){
	tp_node *tn=(tp_node*)etr->private_data;
	etr_sanity_check(etr);
	tp_cache_node *tc=__find_tp_cache_node(tn, lba);
	lru_update(tn->tp_lru, tc->lru_node);
	lru_update(tcm.lru, tn->lru_node);
}

bool tp_is_hit_eviction(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	if(!etr->private_data) return false;
	etr_sanity_check(etr);
	if(tcm.now_caching_byte + TP_ENTRY_SZ > tcm.max_caching_byte){
		return true;
	}
	return false;
}

void tp_update_dynamic_size(struct my_cache*, uint32_t lba, char*data){
	return;
}
