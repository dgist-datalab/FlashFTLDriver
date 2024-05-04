#include "./nocpu_sftl_cache.h"
#include "../../demand_mapping.h"
#include "../../../../include/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include "../../../../include/debug_utils.h"

extern uint32_t debug_lba;
extern uint32_t test_ppa;

nocpu_sftl_cache_monitor nscm;
extern demand_map_manager dmm;

void nocpu_sftl_print_log_idx(my_cache *sc){
       uint64_t total_memory=0;
       for(uint32_t i=0; i< GTDNUM; i++){
               total_memory+=nscm.gtd_size[i];
       }

       printf("SFTL idx memory:%.2lf\n", (double)total_memory/(RANGE*4));
}

static bool nocpu_dump_cache_update(struct my_cache *, GTD_entry *etr, char *data){
	if(!etr->private_data) return false;
	nocpu_sftl_cache *sc=(nocpu_sftl_cache*)((lru_node*)etr->private_data)->data;

	bool target;
	uint32_t max=PAGESIZE/sizeof(uint32_t);
	uint32_t last_ppa=0;
	uint32_t head_idx=0;
	uint32_t *ppa_array=(uint32_t*)data;
	uint32_t ppa_array_idx=0;
	uint32_t offset=0;
	uint32_t total_head=0;

	

	for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
		ppa_array[i]=sc->head_array[i];
	}

	#ifdef FASTSFTLCPU
	nscm.temp_ent[etr->idx].head_array=sc->head_array;
	nscm.temp_ent[etr->idx].run_length=sc->run_length;
	nscm.temp_ent[etr->idx].etr=sc->etr;
	nscm.temp_ent[etr->idx].unpopulated_num=sc->unpopulated_num;
	#else
	free(sc->head_array);
	delete sc->run_length;
	#endif

	lru_delete(nscm.lru, (lru_node*)etr->private_data);
	etr->private_data=NULL;
	nscm.now_caching_byte-=nscm.gtd_size[etr->idx];
}

my_cache nocpu_sftl_cache_func{
	.init=						nocpu_sftl_init,
	.free=						nocpu_sftl_free,
	.is_needed_eviction=			nocpu_sftl_is_needed_eviction,
	.need_more_eviction=			nocpu_sftl_is_needed_eviction,
	.update_eviction_hint=			nocpu_sftl_update_eviction_hint,
	.is_hit_eviction=				nocpu_sftl_is_hit_eviction,
	.update_hit_eviction_hint=		nocpu_sftl_update_hit_eviction_hint,
	.is_eviction_hint_full=			nocpu_sftl_is_eviction_hint_full,
	.get_remain_space=				nocpu_sftl_get_remain_space,
	.update_entry=					nocpu_sftl_update_entry,
	.update_entry_gc=				nocpu_sftl_update_entry_gc,
	.force_put_mru=					nocpu_sftl_force_put_mru,
	.insert_entry_from_translation=	nocpu_sftl_insert_entry_from_translation,
	.update_from_translation_gc=		nocpu_sftl_update_from_translation_gc,
	.get_mapping=						nocpu_sftl_get_mapping,
	.get_eviction_GTD_entry=			nocpu_sftl_get_eviction_GTD_entry,
	.get_eviction_mapping_entry=NULL,
	.update_eviction_target_translation=
										nocpu_sftl_update_eviction_target_translation,
	.evict_target=NULL,
	.dump_cache_update=					nocpu_dump_cache_update,
	.load_specialized_meta=				NULL,
	.update_dynamic_size=				nocpu_sftl_update_dynamic_size,
	.empty_cache=						nocpu_sftl_empty_cache,
	.exist=								nocpu_sftl_exist,
	.print_log=							nocpu_sftl_print_log_idx,
};

static uint32_t *real_mapping;
uint32_t nocpu_sftl_init(struct my_cache *mc, uint32_t total_caching_physical_pages){
	lru_init(&nscm.lru, NULL, NULL);
	nscm.temp_ent.resize(GTDNUM);
	uint32_t half_translate_page_num=RANGE/(PAGESIZE/(sizeof(uint32_t)))/2;
	nscm.max_caching_byte=total_caching_physical_pages * PAGESIZE - half_translate_page_num * (4+4);
	nscm.max_caching_byte-=half_translate_page_num*2/8; //for dirty bit
	nscm.now_caching_byte=0;
	mc->type=COARSE;
	mc->entry_type=DYNAMIC;
	mc->private_data=NULL;
	nscm.gtd_size=(uint32_t*)malloc(GTDNUM *sizeof(uint32_t));
	for(uint32_t i=0; i<GTDNUM; i++){
		nscm.gtd_size[i]=BITMAPSIZE+PAGESIZE;
	}

	real_mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP*L2PGAP);
	for(uint32_t i=0; i<_NOP*L2PGAP; i++){
		real_mapping[i]=UINT32_MAX;
	}

	printf("|\tcaching <min> percentage: %.2lf%%\n", (double) ((nscm.max_caching_byte/(BITMAPSIZE+PAGESIZE)) * BITMAPMEMBER)/ RANGE *100);
	return (nscm.max_caching_byte/(BITMAPSIZE+sizeof(uint32_t))) * BITMAPMEMBER;
}

uint32_t nocpu_sftl_free(struct my_cache *mc){
	uint32_t total_entry_num=0;
	while(1){
		nocpu_sftl_cache *sc=(nocpu_sftl_cache*)lru_pop(nscm.lru);
		if(!sc) break;
		uint32_t total_head=(nscm.gtd_size[sc->etr->idx]-BITMAPSIZE)/sizeof(uint32_t);
		total_entry_num+=PAGESIZE/sizeof(DMF);
		delete sc->run_length;
		free(sc->head_array);
		free(sc);
	}
	printf("now byte:%u max_byte:%u\n", nscm.now_caching_byte, nscm.max_caching_byte);
	printf("cached_entry_num:%u (%lf)\n", total_entry_num, (double)total_entry_num/RANGE);

	uint32_t average_head_num=0;
	uint32_t head_histogram[PAGESIZE/sizeof(uint32_t) + 1]={0,};
	for(uint32_t i=0; i<GTDNUM; i++){
		uint32_t temp_head_num=(nscm.gtd_size[i]-BITMAPSIZE)/sizeof(uint32_t);
		head_histogram[temp_head_num]++;
		average_head_num+=temp_head_num;
	}
	printf("average head num:%lf\n", (double)(average_head_num)/GTDNUM);
	
	for(uint32_t i=0; i<=PAGESIZE/sizeof(uint32_t); i++){
		if(head_histogram[i]){
			printf("%u,%u\n", i, head_histogram[i]);
		}
	}

	lru_free(nscm.lru);
	free(nscm.gtd_size);
	return 1;
}

uint32_t nocpu_sftl_is_needed_eviction(struct my_cache *mc, uint32_t lba, uint32_t *, uint32_t eviction_hint){
	uint32_t target_size=nscm.gtd_size[GETGTDIDX(lba)];
	if(nscm.max_caching_byte <= nscm.now_caching_byte+target_size+sizeof(uint32_t)*2+(eviction_hint)){
		return nscm.now_caching_byte==0? EMPTY_EVICTION : NORMAL_EVICTION;
	}

	if(nscm.max_caching_byte <= nscm.now_caching_byte){
	}
	return HAVE_SPACE;
}

uint32_t nocpu_sftl_update_eviction_hint(struct my_cache *, uint32_t lba, uint32_t * /*prefetching_info*/,uint32_t eviction_hint, 
		uint32_t *now_eviction_hint, bool increase){
	uint32_t target_size=nscm.gtd_size[GETGTDIDX(lba)];
	if(increase){
		*now_eviction_hint=target_size+sizeof(uint32_t)*2;
		return eviction_hint+*now_eviction_hint;
	}
	else{
		return eviction_hint-*now_eviction_hint;
	}
}

inline static nocpu_sftl_cache* get_initial_state_cache(uint32_t gtd_idx, GTD_entry *etr){
	nocpu_sftl_cache *res=(nocpu_sftl_cache *)malloc(sizeof(nocpu_sftl_cache));
	res->head_array=(uint32_t*)malloc(PAGESIZE);
	memset(res->head_array, -1, PAGESIZE);
	res->etr=etr;
	res->run_length=new std::map<int,int>();
	res->unpopulated_num=PAGESIZE/sizeof(uint32_t);
	nscm.gtd_size[gtd_idx]=PAGESIZE+BITMAPSIZE;
	return res;
}

inline static uint32_t get_ppa_from_sc(nocpu_sftl_cache *sc, uint32_t lba){
	if(sc->head_array==NULL){
		return nscm.temp_ent[sc->etr->idx].head_array[GETOFFSET(lba)];
	}
	return sc->head_array[GETOFFSET(lba)];
}

static inline void sftl_size_checker(uint32_t eviction_hint){
	if(nscm.now_caching_byte+eviction_hint> nscm.max_caching_byte/100*110){
		printf("n:%u m:%u e:%ucaching overflow! %s:%d\n", nscm.now_caching_byte, nscm.max_caching_byte, eviction_hint, __FILE__, __LINE__);
		abort();
	}
}

static inline void nocpu_size_checker(nocpu_sftl_cache *sc, int sc_idx){
	return;

	int max=PAGESIZE/sizeof(uint32_t);
	int prev_ppa=-1;
	int unsequential_cnt=0;
	for(int i=0; i<max; i++){
		if(i==0){
			prev_ppa=sc->head_array[i];
			unsequential_cnt++;
		}
		else{
			if(prev_ppa!=-1 && sc->head_array[i]==prev_ppa+1 ){
				prev_ppa++;
			}
			else{
				unsequential_cnt++;
				prev_ppa=sc->head_array[i];
			}
		}
	}
	if(unsequential_cnt*4+BITMAPSIZE != nscm.gtd_size[sc_idx]){
		printf("unsequential_cnt:%u gtd_size:%u\n", unsequential_cnt, nscm.gtd_size[sc_idx]);
		for(int i=0; i<max; i++){
			printf("%u %u\n", i, sc->head_array[i]);
		}
		abort();
	}
}

enum NEXT_STEP{
	DONE, SHRINK, EXPAND
};

void update_run_length(std::map<int,int> *run_length, uint32_t offset, uint32_t ppa, uint32_t *head_array){
	if(run_length->size()==0){
		run_length->insert(std::make_pair(offset, offset));
	}
	else{
		std::map<int,int>::iterator it=run_length->upper_bound(offset);
		it--;
		if(it->first > offset){
			//no consecutive
			run_length->insert(std::make_pair(offset, offset));
		}
		else if(it->second>offset){
			if(it->first==offset){
				//update head
				uint32_t original_end=it->second;
				it->second=offset;
				if(offset+1<=original_end){
					run_length->insert(std::make_pair(offset+1, original_end));
				}
			}
			else{
				//update inside

				//update forward
				uint32_t original_end=it->second;
				it->second=offset-1;
				if(offset+1<=original_end){
					//insert new for backward
					run_length->insert(std::make_pair(offset+1, original_end));
				}
				//insert target
				run_length->insert(std::make_pair(offset, offset));
			}
		}
		else{ //-->second==offset or no entry for offset
			if(it->second+1==offset && head_array[offset-1]+1==ppa){
				//consecutive previous header
				it->second=offset;
			}
			else{
				uint32_t target_end=offset;
				std::map<int,int>::iterator next=it;
				next++;
				if(next!=run_length->end()){
					uint32_t next_ppa=head_array[next->first];
					if(next->first==offset+1 && ppa+1==next_ppa){
						target_end=next->second;
						run_length->erase(next);
					}
				}

				if(it->second==offset && it->first!=offset){
					it->second=offset-1;
					run_length->insert(std::make_pair(offset, target_end));
				}
				else if(it->second==offset && it->first==offset){
					if (offset != 0){
						if (head_array[offset - 1] + 1 == ppa){
							// update backward
							std::map<int, int>::iterator prev = it;
							prev--;
							if (prev->second + 1 == offset){
								prev->second = offset;
							}
							run_length->erase(it);
						}
						else{
							//do nothing --> same as the original
						}
					}
					else{
						//do nothing --> same as the original
					}
				}
				else{ 
					//no entry for offset
					run_length->insert(std::make_pair(offset, target_end));
				}
			}
		}
	}
}

uint32_t __update_entry(GTD_entry *etr, uint32_t lba, uint32_t ppa, bool isgc, uint32_t *eviction_hint){
	nocpu_sftl_cache *sc;

	static int cnt=0;
	if(cnt++==44528008){
	//	GDB_MAKE_BREAKPOINT;
	}
	//printf("ttt :%u\n", cnt++);

	if(lba==1789965){
		//GDB_MAKE_BREAKPOINT;
	}
	uint32_t old_ppa;
	uint32_t gtd_idx=GETGTDIDX(lba);
	int32_t prev_gtd_size;
	int32_t changed_gtd_size;
	lru_node *ln;

	real_mapping[lba]=ppa;

	if(etr->status==EMPTY){
		sc=get_initial_state_cache(gtd_idx, etr);
		ln=lru_push(nscm.lru, sc);
		etr->private_data=(void*)ln;
	}else{
		if(nscm.now_caching_byte <= nscm.gtd_size[gtd_idx]){
			nscm.now_caching_byte=0;
		}
		else{
			nscm.now_caching_byte-=nscm.gtd_size[gtd_idx];
		}
		if(etr->private_data==NULL){
			printf("insert translation page before cache update! %s:%d\n",__FILE__, __LINE__);
			//print_stacktrace();
			abort();
		}
		ln=(lru_node*)etr->private_data;
		sc=(nocpu_sftl_cache*)(ln->data);
	}

	prev_gtd_size=nscm.gtd_size[gtd_idx];
	old_ppa=get_ppa_from_sc(sc, lba);
	sc->head_array[GETOFFSET(lba)]=ppa;

	int offset=GETOFFSET(lba);
	update_run_length(sc->run_length, offset, ppa, sc->head_array);

	if(old_ppa==UINT32_MAX && sc->unpopulated_num){
		sc->unpopulated_num--;
	}

	nscm.gtd_size[gtd_idx]=(sc->run_length->size()+sc->unpopulated_num)*sizeof(uint32_t)+BITMAPSIZE;
	changed_gtd_size=nscm.gtd_size[gtd_idx];

	if(changed_gtd_size - prev_gtd_size > (int)sizeof(uint32_t)*2){
		printf("what happen???\n");
		abort();
	}

	nscm.now_caching_byte+=nscm.gtd_size[gtd_idx];

	nocpu_size_checker(sc, gtd_idx);
	
	if(eviction_hint){
		sftl_size_checker(*eviction_hint);
	}
	else{
		sftl_size_checker(0);
	}

	if(!isgc){
		lru_update(nscm.lru, ln);
	}
	etr->status=DIRTY;
	return old_ppa;
}
extern uint32_t test_ppa;
uint32_t nocpu_sftl_update_entry(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa, uint32_t *eviction_hint){
	//if(etr->idx==56 && ppa!=UINT32_MAX){
	//	GDB_MAKE_BREAKPOINT;
	//}

	return __update_entry(etr, lba, ppa, false, eviction_hint);
}

uint32_t nocpu_sftl_update_entry_gc(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa){
	//if(etr->idx==56 && ppa!=UINT32_MAX){
	//	GDB_MAKE_BREAKPOINT;
	//}
	return __update_entry(etr, lba, ppa, true, NULL);
}

static inline nocpu_sftl_cache *make_sc_from_translation(GTD_entry *etr, char *data){
	#ifdef FASTSFTLCPU
	nocpu_sftl_cache *sc=(nocpu_sftl_cache*)malloc(sizeof(nocpu_sftl_cache));
	nocpu_sftl_cache &t=nscm.temp_ent[etr->idx];
	sc->head_array=t.head_array;
	sc->run_length=t.run_length;
	sc->etr=etr;
	sc->unpopulated_num=t.unpopulated_num;

	t.head_array=NULL;
	t.run_length=NULL;
	t.unpopulated_num=0;
	t.etr=NULL;
	#else
	nocpu_sftl_cache *sc=get_initial_state_cache(etr->idx, etr);
	memcpy(sc->head_array, data, PAGESIZE);
	uint32_t prev_ppa=-1;
	uint32_t start_offset=-1;

	for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
		if(sc->head_array[i]!=UINT32_MAX){
			sc->unpopulated_num--;
		}
		else{
			if(start_offset!=-1){
				sc->run_length->insert(std::make_pair(start_offset, i-1));
				start_offset=-1;
			}
			continue;
		}
		
		if(start_offset==-1){
			start_offset=i;
			prev_ppa=sc->head_array[i];
		}
		else if(prev_ppa+1==sc->head_array[i]){
			prev_ppa++;
		}
		else{
			sc->run_length->insert(std::make_pair(start_offset, i-1));
			start_offset=i;
			prev_ppa=sc->head_array[i];
		}
	}
	if(start_offset!=-1){
		sc->run_length->insert(std::make_pair(start_offset, PAGESIZE/sizeof(uint32_t)-1));
	}
	nscm.gtd_size[etr->idx]=(sc->run_length->size()+sc->unpopulated_num)*sizeof(uint32_t)+BITMAPSIZE;
	nocpu_size_checker(sc, etr->idx);
	#endif
	return sc;
}

uint32_t nocpu_sftl_insert_entry_from_translation(struct my_cache *, GTD_entry *etr, uint32_t /*lba*/, char *data, uint32_t *eviction_hint, uint32_t org_eviction_hint){
	if(etr->private_data){
		printf("already lru node exists! %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	nocpu_sftl_cache *sc=make_sc_from_translation(etr, data);

	etr->private_data=(void *)lru_push(nscm.lru, (void*)sc);
	etr->status=CLEAN;
	nscm.now_caching_byte+=nscm.gtd_size[etr->idx];

	uint32_t target_size=nscm.gtd_size[etr->idx];
	(*eviction_hint)-=org_eviction_hint;

	sftl_size_checker(*eviction_hint);
	return 1;
}

uint32_t nocpu_sftl_update_from_translation_gc(struct my_cache *, char *data, uint32_t lba, uint32_t ppa){
	uint32_t *ppa_list=(uint32_t*)data;
	uint32_t old_ppa=ppa_list[GETOFFSET(lba)];
	ppa_list[GETOFFSET(lba)]=ppa;

	nscm.temp_ent[GETGTDIDX(lba)].head_array[GETOFFSET(lba)]=ppa;
	real_mapping[lba]=ppa;
	
	uint32_t gtd_idx=GETGTDIDX(lba);
	update_run_length(nscm.temp_ent[gtd_idx].run_length, GETOFFSET(lba), ppa, nscm.temp_ent[gtd_idx].head_array);

	return old_ppa;
}

void nocpu_sftl_update_dynamic_size2(struct my_cache *, uint32_t lba, char *data){
	//if(lba/(PAGESIZE/sizeof(uint32_t))==56){
	//	GDB_MAKE_BREAKPOINT;
	//}
	uint32_t total_head=0;
	uint32_t last_ppa=0;
	bool sequential_flag=false;
	uint32_t *ppa_list=(uint32_t*)data;
	for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
		if(i==0){
			last_ppa=ppa_list[i];
			total_head++;
		}
		else{
			sequential_flag=false;
			if(last_ppa+1==ppa_list[i]){
				sequential_flag=true;
			}

			if(sequential_flag){
				last_ppa++;
			}
			else{
				last_ppa=ppa_list[i];
				total_head++;
			}
		}
	}
	if(total_head<1 || total_head>PAGESIZE/sizeof(uint32_t)){
		printf("total_head over or small %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	nscm.gtd_size[GETGTDIDX(lba)]=(total_head*sizeof(uint32_t)+BITMAPSIZE);
}

void nocpu_sftl_update_dynamic_size(struct my_cache *, uint32_t lba, char *data){
	return;
	//if(lba/(PAGESIZE/sizeof(uint32_t))==56){
	//	GDB_MAKE_BREAKPOINT;
	//}
	uint32_t total_head=0;
	uint32_t last_ppa=0;
	uint32_t offset=0;
	bool sequential_flag=false;
	uint32_t *ppa_list=(uint32_t*)data;

	uint32_t gtd_idx=GETGTDIDX(lba);
	nscm.temp_ent[gtd_idx].run_length->clear();

	for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
		if(i==0){
			last_ppa=ppa_list[i];
			total_head++;
			offset=i;
		}
		else{
			sequential_flag=false;
			if(last_ppa+1==ppa_list[i]){
				sequential_flag=true;
			}

			if(sequential_flag){
				last_ppa++;
			}
			else{
				//nscm.temp_ent[gtd_idx].run_length->insert(std::make_pair(offset, i-1));
				last_ppa=ppa_list[i];
				total_head++;
				offset=i;
			}
		}
	}

	//nscm.temp_ent[gtd_idx].run_length->insert(std::make_pair(offset, PAGESIZE/sizeof(uint32_t)-1));

	if(total_head!=nscm.temp_ent[gtd_idx].run_length->size()){
		//printf("total_head:%u run_length:%u\n", total_head, nscm.temp_ent[gtd_idx].run_length->size());
		//abort();
	}

	if(total_head<1 || total_head>PAGESIZE/sizeof(uint32_t)){
		//printf("total_head over or small %s:%d\n", __FILE__, __LINE__);
		//abort();
	}
	nscm.gtd_size[GETGTDIDX(lba)]=(nscm.temp_ent[gtd_idx].run_length->size()+nscm.temp_ent[gtd_idx].unpopulated_num)*sizeof(uint32_t)+BITMAPSIZE;
}

uint32_t nocpu_sftl_get_mapping(struct my_cache *, uint32_t lba){
//	return real_mapping[lba];

	uint32_t gtd_idx=GETGTDIDX(lba);
	GTD_entry *etr=&dmm.GTD[gtd_idx];
	if(!etr->private_data){
		printf("insert data before pick mapping! %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	return get_ppa_from_sc((nocpu_sftl_cache*)((lru_node*)etr->private_data)->data, lba);
}

struct GTD_entry *nocpu_sftl_get_eviction_GTD_entry(struct my_cache *, uint32_t lba){
	lru_node *target;
	GTD_entry *etr=NULL;
	for_each_lru_backword(nscm.lru, target){
		nocpu_sftl_cache *sc=(nocpu_sftl_cache*)target->data;
		etr=sc->etr;
		
		if(etr->status==FLYING || etr->status==EVICTING){
			continue;
		}
		if(etr->status==CLEAN){
			etr->private_data=NULL;
			sc=(nocpu_sftl_cache*)target->data;
			#ifdef FASTSFTLCPU
			nscm.temp_ent[etr->idx].head_array=sc->head_array;
			nscm.temp_ent[etr->idx].run_length=sc->run_length;
			nscm.temp_ent[etr->idx].etr=sc->etr;
			nscm.temp_ent[etr->idx].unpopulated_num=sc->unpopulated_num;
			#else
			free(sc->head_array);
			delete sc->run_length;
			#endif
			lru_delete(nscm.lru, target);
			nscm.now_caching_byte-=nscm.gtd_size[etr->idx];
			free(sc);
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


bool nocpu_sftl_update_eviction_target_translation(struct my_cache* ,uint32_t,  GTD_entry *etr,mapping_entry *map, char *data, void *, bool){
	nocpu_sftl_cache *sc=(nocpu_sftl_cache*)((lru_node*)etr->private_data)->data;

	memcpy(data, sc->head_array, PAGESIZE);
	#ifdef FASTSFTLCPU
	nscm.temp_ent[etr->idx].head_array=sc->head_array;
	nscm.temp_ent[etr->idx].run_length=sc->run_length;
	nscm.temp_ent[etr->idx].etr=sc->etr;
	nscm.temp_ent[etr->idx].unpopulated_num=sc->unpopulated_num;
	#else
	free(sc->head_array);
	delete sc->run_length;
	#endif
	free(sc);

	lru_delete(nscm.lru, (lru_node*)etr->private_data);
	etr->private_data=NULL;
	nscm.now_caching_byte-=nscm.gtd_size[etr->idx];
	return true;
}

bool nocpu_sftl_exist(struct my_cache *, uint32_t lba){
	return dmm.GTD[GETGTDIDX(lba)].private_data!=NULL;
}

bool nocpu_sftl_is_hit_eviction(struct my_cache *, GTD_entry *etr, uint32_t lba, uint32_t ppa, uint32_t total_hit_eviction){
	if(!etr->private_data) return false;
	nocpu_sftl_cache *sc=GETSCFROMETR(etr);
	uint32_t offset=GETOFFSET(lba);
	if(offset!=0 && sc->head_array[offset-1] !=UINT32_MAX && sc->head_array[offset-1]+1==ppa){
		return false;
	}

	if(nscm.now_caching_byte+total_hit_eviction+sizeof(uint32_t)*2 > nscm.max_caching_byte){
		return true;
	}
	return false;
}

void nocpu_sftl_force_put_mru(struct my_cache *, GTD_entry *etr,mapping_entry *map, uint32_t lba){
	lru_update(nscm.lru, (lru_node*)etr->private_data);
}

bool nocpu_sftl_is_eviction_hint_full(struct my_cache *, uint32_t eviction_hint){
	return nscm.max_caching_byte <= eviction_hint;
}

uint32_t nocpu_sftl_update_hit_eviction_hint(struct my_cache *, uint32_t lba, uint32_t *prefetching_info, uint32_t eviction_hint, 
		uint32_t *now_eviction_hint, bool increase){
	if(increase){
		*now_eviction_hint=sizeof(uint32_t)*2;
		return eviction_hint+*now_eviction_hint;
	}else{
		return eviction_hint-*now_eviction_hint;
	}
}

int32_t nocpu_sftl_get_remain_space(struct my_cache *, uint32_t total_eviction_hint){
	return nscm.max_caching_byte-nscm.now_caching_byte-total_eviction_hint;
}

void nocpu_sftl_empty_cache(struct my_cache *mc){
	uint32_t total_entry_num=0;
	while(1){
		nocpu_sftl_cache *sc=(nocpu_sftl_cache*)lru_pop(nscm.lru);
		if(!sc) break;
		uint32_t total_head=(nscm.gtd_size[sc->etr->idx]-BITMAPSIZE)/sizeof(uint32_t);
		total_entry_num+=PAGESIZE/sizeof(DMF);
		#ifdef FASTSFTLCPU
		nscm.temp_ent[sc->etr->idx].head_array=sc->head_array;
		nscm.temp_ent[sc->etr->idx].run_length=sc->run_length;
		nscm.temp_ent[sc->etr->idx].etr=sc->etr;
		nscm.temp_ent[sc->etr->idx].unpopulated_num=sc->unpopulated_num;
		#else
		free(sc->head_array);
		delete sc->run_length;
		#endif
		sc->etr->private_data=NULL;
		free(sc);
	}

	uint32_t average_head_num=0;
	for(uint32_t i=0; i<GTDNUM; i++){
		uint32_t temp_head_num=(nscm.gtd_size[i]-BITMAPSIZE)/sizeof(uint32_t);
	//	pritnf("%u -> %u\n", i, temp_head_num);
		average_head_num+=temp_head_num;
	}
	printf("cached_entry_num:%u (%lf)\n", total_entry_num, (double)total_entry_num/RANGE);
	printf("average head num:%lf\n", (double)(average_head_num)/GTDNUM);
	
	uint64_t total_index_memory=0;
	for(uint32_t i=0; i<GTDNUM; i++){
		total_index_memory+=nscm.gtd_size[i];
	}
	printf("total index memory:%lu\n", total_index_memory);
	
	nscm.now_caching_byte=0;
}
