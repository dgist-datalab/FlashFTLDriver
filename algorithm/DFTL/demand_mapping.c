#include "demand_mapping.h"
#include "page.h"
#include "gc.h"
#include <stdlib.h>
#include <getopt.h>
#include <stdint.h>
extern uint32_t test_key;
demand_map_manager dmm;
dmi DMI;


inline static void cpy_keys(uint32_t **des, uint32_t *src){
	(*des)=(uint32_t*)malloc(sizeof(KEYT)*L2PGAP);
	memcpy((*des), src, sizeof(KEYT)*L2PGAP);
}

static inline char *cache_type(cache_algo_type t){
	switch(t){
		case DEMAND_COARSE: return "DEMAND_COARSE";
		case DEMAND_FINE: return "DEMAND_FINE";
		case SFTL: return "SFTL";
		case TPFTL: return "TPFTL";
		default:
			abort();
	}
}

void demand_map_create(uint32_t total_caching_physical_pages, lower_info *li, blockmanager *bm){
	uint32_t total_logical_page_num;
	uint32_t total_translation_page_num;

	total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));

	if(total_caching_physical_pages!=UINT32_MAX){
		dmm.max_caching_pages=total_caching_physical_pages;
	}
	else{

	}

	dmm.GTD=(GTD_entry*)calloc(total_translation_page_num,sizeof(GTD_entry));
	for(uint32_t i=0; i<total_translation_page_num; i++){
		fdriver_mutex_init(&dmm.GTD[i].lock);
		dmm.GTD[i].idx=i;
		dmm.GTD[i].pending_req=list_init();
		dmm.GTD[i].physical_address=UINT32_MAX;
	}

	dmm.li=li;
	dmm.bm=bm;


	printf("--------------------\n");
	printf("|DFTL settings\n");
	printf("|cache type: %s\n", cache_type(dmm.c_type));
	printf("|cache size: %.2lf(mb)\n", ((double)dmm.max_caching_pages * PAGESIZE)/M);
	printf("|\tratio of PFTL: %.2lf%%\n", ((double)dmm.max_caching_pages * PAGESIZE)/(SHOWINGSIZE/K)*100);

	uint32_t cached_entry=dmm.cache->init(dmm.cache, dmm.max_caching_pages);
	printf("|\tcaching percentage: %.2lf%%\n", (double)cached_entry/total_logical_page_num *100);
	printf("--------------------\n");
}

void demand_map_free(){
	printf("===========cache results========\n");
	printf("Cache miss num: %u\n",DMI.miss_num);
	printf("\tCache cold miss num: %u\n",DMI.cold_miss_num);
	printf("\tCache capacity miss num: %u\n",DMI.miss_num - DMI.cold_miss_num);
	printf("Cache hit num: %u\n",DMI.hit_num);
	printf("Cache hit ratio: %.2lf%%\n", (double) DMI.hit_num / (DMI.miss_num+DMI.hit_num) * 100);

	printf("\nCache eviction cnt: %u\n", DMI.eviction_cnt);
	printf("\tEviction clean cnt: %u\n", DMI.clean_eviction);
	printf("\tEviction dirty cnt: %u\n", DMI.dirty_eviction);
	printf("===============================\n");

	uint32_t total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	uint32_t total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));

	for(uint32_t i=0; i<total_translation_page_num; i++){
		fdriver_destroy(&dmm.GTD[i].lock);
		list_free(dmm.GTD[i].pending_req);
	}
	free(dmm.GTD);
	dmm.cache->free(dmm.cache);
}

uint32_t map_read_wrapper(GTD_entry *etr, request *req, lower_info *, void *params){
	if(etr->status==FLYING){
		fdriver_lock(&etr->lock);
		list_insert(etr->pending_req, (void*)req);
		fdriver_unlock(&etr->lock);
		return 1;
	}else{
		fdriver_lock(&etr->lock);
		etr->status=FLYING;
		demand_mapping_read(etr->physical_address/L2PGAP, dmm.li, req, params);
		list_insert(etr->pending_req, (void*)req);
		fdriver_unlock(&etr->lock);
		return 0;
	}
}

inline void __demand_map_pending_read(request *req, demand_params *dp, pick_params_ex *pp){
	uint32_t ppa=dmm.cache->get_mapping(dmm.cache, req->key);
	if(ppa==UINT32_MAX){
		printf("try to read invalidate ppa %s:%d\n", __FILE__,__LINE__);
		abort();
	}
	if(req->key==test_key){
		printf("%u:%u read data\n", req->key, ppa);
	}
	req->value->ppa=ppa;
	send_user_req(req, DATAR, ppa/L2PGAP, req->value);
	free(pp);
	free(dp);
}

uint32_t demand_map_coarse_type_pending(request *req, GTD_entry *etr, char *value){
	list_node *now, *next;
	request *treq;
	KEYT* lba;
	KEYT* physical;
	demand_params *dp;
	assign_params_ex *ap;
	pick_params_ex *pp;
	uint32_t old_ppa;
	uint8_t i;

	uint32_t res=0;
	dmm.cache->insert_entry_from_translation(dmm.cache, etr, UINT32_MAX, value);

	fdriver_lock(&etr->lock);
	for_each_list_node_safe(etr->pending_req, now, next){
		treq=(request*)now->data;
		dp=(demand_params*)treq->params;
		ap=NULL;
		pp=NULL;

		if(treq->type==FS_SET_T && dp->status==MISSR){
			ap=(assign_params_ex*)dp->params_ex;
			lba=ap->lba;
			physical=ap->physical;
			i=ap->idx;
			old_ppa=dmm.cache->update_entry(dmm.cache, etr, lba[i], physical[i]);
			if(old_ppa!=UINT32_MAX){
				invalidate_ppa(old_ppa);
			}

			i++;
			if(i==L2PGAP){
				free(lba);
				free(physical);
				free(ap);
				free(dp);

				treq->end_req(treq);
				if(req==treq){
					res=1;	
				}
			}
			else{
				if(req==treq){
					list_delete_node(etr->pending_req, now);
					dp->status=NONE;
					ap->idx=i;
					continue;
				}
				dp->status=NONE;
				ap->idx=i;
				inf_assign_try(treq);
			}
		}
		else if(treq->type==FS_GET_T){
			if(req==treq){
				res=1;			
			}
			__demand_map_pending_read(treq, dp, pp);
		}
		else{
			printf("unknown type! %s:%d\n", __FILE__, __LINE__);
			abort();
		}

		list_delete_node(etr->pending_req, now);

	}
	fdriver_unlock(&etr->lock);
	return res;
}

uint32_t demand_map_fine_type_pending(request *const req, mapping_entry *mapping, char *value){
	list_node *now, *next;
	request *treq;
	KEYT* lba=NULL;
	KEYT* physical=NULL;
	demand_params *dp;
	assign_params_ex *ap;
	pick_params_ex *pp;
	uint32_t old_ppa;
	uint8_t i=0;

	GTD_entry *etr=&dmm.GTD[GETGTDIDX(mapping->lba)];
	uint32_t res=0;

	bool processed=false;
	fdriver_lock(&etr->lock);
	for_each_list_node_safe(etr->pending_req, now, next){ //for read
		treq=(request*)now->data;
		dp=(demand_params*)treq->params;
		ap=NULL;
		pp=NULL;

		if(dp->status==MISSR){
			if(treq->type==FS_SET_T){
				printf("FINE type doesn't need to read mapping in SET %s:%d\n", __FILE__, __LINE__);
				abort();
			}
			if(treq==req) res=1;

			dmm.cache->insert_entry_from_translation(dmm.cache, etr, mapping->lba, value);
			__demand_map_pending_read(treq, dp, pp);

			list_delete_node(etr->pending_req, now);
		}
	}
	
	if(etr->pending_req->size==0) goto end;

	for_each_list_node_safe(etr->pending_req, now, next){ //for eviction
		treq=(request*)now->data;
		dp=(demand_params*)treq->params;
		ap=NULL;
		pp=NULL;
		if(dp->status!=EVICTIONR){
			printf("it can't be %s:%d\n", __FILE__, __LINE__);
			abort();
		}

		if(!processed){
			processed=true;
			dmm.cache->update_eviction_target_translation(dmm.cache, NULL, dp->et.mapping, value);
		}

		if(treq->type==FS_SET_T){
			if(req==treq){
				list_delete_node(etr->pending_req, now);
				continue;
			}
			else{
				dmm.cache->evict_target(dmm.cache, NULL, dp->et.mapping);
				dp->status=EVICTIONW;
				inf_assign_try(treq);
			}
		}
		else{
			if(req==treq){
				list_delete_node(etr->pending_req, now);
				continue;
			}

			dp->status=MISSR;
			map_read_wrapper(dp->etr, treq, dmm.li, dp);
		}
		list_delete_node(etr->pending_req, now);
	}
end:
	etr->status=POPULATE;
	fdriver_unlock(&etr->lock);
	return res;
}

uint32_t demand_map_assign(request *req, KEYT *_lba, KEYT *_physical){
	//static int cnt=0;
	//printf("map_assign called %d %u\n", cnt++, _lba[0]);
//	printf("req->key:%u\n", req->key);
	uint8_t i=0;
	demand_params *dp;
	assign_params_ex *mp;

	uint32_t gtd_idx;
	uint32_t trans_offset;
	GTD_entry *etr;
	uint32_t old_ppa;
	KEYT *lba=NULL, *physical=NULL;

	if(!req->params){
		dp=(demand_params*)malloc(sizeof(demand_params));
		dp->status=NONE;
		lba=_lba; physical=_physical;
		gtd_idx=GETGTDIDX(lba[i]);
		trans_offset=TRANSOFFSET(lba[i]);
		etr=dp->etr=&dmm.GTD[gtd_idx];

		mp=(assign_params_ex*)malloc(sizeof(assign_params_ex));
		cpy_keys(&mp->lba,_lba);
		cpy_keys(&mp->physical, _physical);
		i=mp->idx=0;
		dp->params_ex=(void*)mp;

		req->params=(void*)dp;
	}
	else{
		dp=(demand_params*)req->params;
		mp=(assign_params_ex*)dp->params_ex;

		i=mp->idx;
		lba=mp->lba;
		physical=mp->physical;
		etr=dp->etr;
	}
	
	GTD_entry *target_etr;

	for(;i<L2PGAP; i++){
		gtd_idx=GETGTDIDX(lba[i]);
		trans_offset=TRANSOFFSET(lba[i]);
		etr=&dmm.GTD[gtd_idx];

		dp->etr=etr;
		mp->idx=i;
		mapping_entry *target=&dp->target;
		target->lba=lba[i];
		target->ppa=physical[i];
retry:
		switch(dp->status){
			case EVICTIONW:
				dp->status=MISSR;
				if(dmm.cache->type==FINE){
					dp->status=HIT;
					goto retry;
				}
				else{
					if(etr->status==EMPTY){
						dp->status=HIT;
						goto retry;
					}
				}
				return map_read_wrapper(etr, req, dmm.li, (void*)dp);
			case NONE:
				if(!dmm.cache->exist(dmm.cache, lba[i])){
					DMI.miss_num++;
					if(dmm.cache->is_needed_eviction(dmm.cache, lba[i])){
						DMI.eviction_cnt++;
						dp->status=EVICTIONR; //it is dirty
						if(dmm.cache->type==COARSE){
							dp->et.gtd=dmm.cache->get_eviction_GTD_entry(dmm.cache);
							if(!dp->et.gtd){
								DMI.clean_eviction++;
								dp->status=EVICTIONW;
							}
							else{
								DMI.dirty_eviction++;							
							}
							goto retry;
						}
						else{
							dp->et.mapping=dmm.cache->get_eviction_mapping_entry(dmm.cache);
							if(!dp->et.mapping){
								dp->status=HIT;
								DMI.clean_eviction++;
								goto retry;
							}
							else{
								DMI.dirty_eviction++;							
							}
							target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];

							if(target_etr->physical_address==UINT32_MAX){
								dp->status=EVICTIONR;
								fdriver_lock(&target_etr->lock);
								list_insert(target_etr->pending_req, (void*)req);
								fdriver_unlock(&target_etr->lock);
								goto retry;
							}
							else{
								return map_read_wrapper(target_etr, req, dmm.li,(void*)dp);
							}
						}
					}
					else{
						DMI.cold_miss_num++;
					}

					if(etr->status!=EMPTY){
						dp->status=MISSR;
						if(dmm.cache->type==COARSE){
							return map_read_wrapper(etr, req, dmm.li, (void*)dp);
						}
					}
				}
				else{
					DMI.hit_num++;
				}
				dp->status=HIT;
			case HIT:
				old_ppa=dmm.cache->update_entry(dmm.cache, etr, target->lba, target->ppa);
				if(old_ppa!=UINT32_MAX){
					invalidate_ppa(old_ppa);
				}
				dp->status=NONE;
				break;
			case EVICTIONR:
				if(dmm.cache->type==COARSE){
					dmm.cache->update_eviction_target_translation(dmm.cache, dp->et.gtd, NULL,  req->value->value);
					target_etr=dp->et.gtd;
				}
				else{
					target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
					demand_map_fine_type_pending(req, dp->et.mapping, req->value->value);
				}
				
				if(dmm.cache->entry_type==DYNAMIC && dmm.cache->need_more_eviction(dmm.cache,lba[i])){
					dp->status=NONE;
				}
				else{
					dp->status=EVICTIONW;
				}
				
				if(target_etr->physical_address!=UINT32_MAX){
					invalidate_ppa(target_etr->physical_address);
				}

				target_etr->physical_address=get_map_ppa(gtd_idx)*L2PGAP;
				demand_mapping_write(target_etr->physical_address/L2PGAP, dmm.li, req, (void*)dp);
				return 1;
			case MISSR:
				if(dmm.cache->type==FINE){
					printf("fine cache can't be this status %s:%d\n", __FILE__, __LINE__);
					abort();
					return 1;
				}
				else if(demand_map_coarse_type_pending(req, etr, req->value->value)==1){
					return 1;
				}
				dp->status=NONE;
				break;
		}
	}
	
	if(i!=L2PGAP){
		return 0;
	}
	if(i==L2PGAP){
		if(req->params){
			free(mp->lba);
			free(mp->physical);
			free(mp);
			free(req->params);
		}
		req->end_req(req);
		return 1;
	}
	return 0;
}


uint32_t demand_page_read(request *const req){
	demand_params *dp;
	mapping_entry target;
	uint32_t gtd_idx, trans_offset;
	uint32_t ppa;
	GTD_entry *etr;

	/*/if(req->key==1778630){
		printf("read break %u!\n", req->seq);
		if(req->seq==2100346){
			printf("break!\n");
		}
	}*/

	if(!req->params){
		dp=(demand_params*)malloc(sizeof(demand_params));
		dp->status=NONE;
		gtd_idx=GETGTDIDX(req->key);
		trans_offset=TRANSOFFSET(req->key);
		etr=dp->etr=&dmm.GTD[gtd_idx];
		req->params=(void*)dp;
	}else{
		dp=(demand_params*)req->params;
		etr=dp->etr;
	}

	GTD_entry *target_etr;
retry:
	switch(dp->status){
		case EVICTIONW:
			dp->status=MISSR;
			return map_read_wrapper(etr, req, dmm.li, dp);
		case NONE:
			if(dmm.cache->type==COARSE && etr->status==EMPTY){
				printf("%s:%d non populate error!\n", __FILE__,__LINE__);
				abort();
			}

			if(!dmm.cache->exist(dmm.cache, req->key)){//cache miss
				DMI.miss_num++;	
				if(dmm.cache->is_needed_eviction(dmm.cache, req->key)){
					DMI.eviction_cnt++;
					if(dmm.cache->type==COARSE){
						dp->et.gtd=dmm.cache->get_eviction_GTD_entry(dmm.cache);
						if(!dp->et.gtd){
							dp->status=EVICTIONW; // it is clean
							DMI.clean_eviction++;
						}
						else{
							dp->status=EVICTIONR; //it is dirty
							DMI.dirty_eviction++;
						}
						goto retry;						
					}
					else{
						dp->et.mapping=dmm.cache->get_eviction_mapping_entry(dmm.cache);
						if(!dp->et.mapping){
							dp->status=EVICTIONW;
							DMI.clean_eviction++;
							goto retry;
						}
						else{
							DMI.dirty_eviction++;
						}
						target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
						dp->status=EVICTIONR;
						if(target_etr->physical_address==UINT32_MAX){
							fdriver_lock(&target_etr->lock);
							list_insert(target_etr->pending_req, (void*)req);
							fdriver_unlock(&target_etr->lock);
							goto retry;
						}
						else{
							return map_read_wrapper(target_etr, req, dmm.li,(void*)dp);
						}
					}
					dp->status=MISSR;
					return map_read_wrapper(etr, req, dmm.li, (void*)dp);
				}
				else{
					DMI.cold_miss_num++;
				}
			}
			else{
				DMI.hit_num++;	
			}
			dp->status=HIT;
		case HIT:
			ppa=dmm.cache->get_mapping(dmm.cache, req->key);
			goto read_data;
		case EVICTIONR:
			if(dmm.cache->type==COARSE){
				dmm.cache->update_eviction_target_translation(dmm.cache, dp->et.gtd, NULL, req->value->value);
				target_etr=dp->et.gtd;
			}
			else{
				target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
				demand_map_fine_type_pending(req, dp->et.mapping, req->value->value);
			}

			if(dmm.cache->entry_type==DYNAMIC && dmm.cache->need_more_eviction(dmm.cache, req->key)){
				dp->status=NONE;
			}
			else{
				dp->status=EVICTIONW;
			}

			if(target_etr->physical_address!=UINT32_MAX)
				invalidate_ppa(target_etr->physical_address);
			target_etr->physical_address=get_map_ppa(gtd_idx) * L2PGAP;
			demand_mapping_write(target_etr->physical_address/L2PGAP, dmm.li, req, dp);
			return 1;
		case MISSR:
			if(dmm.cache->type==FINE){
				target.lba=req->key;
				if((demand_map_fine_type_pending(req, &target, req->value->value)==1)){
					goto end;
				}
			}
			else if(demand_map_coarse_type_pending(req, etr, req->value->value)==1){ //this is issue read request
				goto end;
			}
	}

read_data:
	if(ppa==UINT32_MAX){
		printf("try to read invalidate ppa %s:%d\n", __FILE__,__LINE__);
		abort();
	}
	if(req->key==test_key){
		printf("%u:%u read data\n", req->key, ppa);
	}
	req->value->ppa=ppa;
	free(req->params);
	send_user_req(req, DATAR, ppa/L2PGAP, req->value);
end:
	return 1;
}

int mapping_entry_compare(const void * a, const void *b){
	return (int)((mapping_entry*)a)->lba - (int)((mapping_entry*)b)->lba;
}

uint32_t demand_map_some_update(mapping_entry *target, uint32_t idx){ //for gc
	qsort(target, idx, sizeof(mapping_entry), mapping_entry_compare);
	bool read_start=false;
	uint32_t gtd_idx=0, temp_gtd_idx;
	gc_map_value *gmv;
	list *temp_list=list_init();
	uint32_t old_ppa;
	for(uint32_t i=0; i<idx; i++){
		temp_gtd_idx=GETGTDIDX(target[i].lba);
		if(dmm.cache->exist(dmm.cache, target[i].lba) && target[i].ppa!=UINT32_MAX){
			old_ppa=dmm.cache->update_entry_gc(dmm.cache, &dmm.GTD[temp_gtd_idx], target[i].lba, target[i].ppa);
			if(old_ppa!=UINT32_MAX){
				invalidate_ppa(old_ppa);
			}
		}else{
			if(read_start && gtd_idx==temp_gtd_idx) continue;
			if(!read_start){read_start=true;}

			gtd_idx=temp_gtd_idx;

			gmv=(gc_map_value*)malloc(sizeof(gc_map_value));
			gmv->pair=target[i];
			gmv->start_idx=i;
			gmv->gtd_idx=gtd_idx;
			gmv->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			gmv->isdone=false;
			list_insert(temp_list, (void*)gmv);
			demand_mapping_inter_read(dmm.GTD[gtd_idx].physical_address/L2PGAP, dmm.li, gmv);
		}
	}
	

	list_node *now, *nxt;
	while(temp_list->size){
		for_each_list_node_safe(temp_list, now, nxt){

			gmv=(gc_map_value*)now->data;
			if(!gmv->isdone) continue;

			gtd_idx=gmv->gtd_idx;
			for(uint32_t i=gmv->start_idx; GETGTDIDX(target[i].lba)==gtd_idx; i++){
				dmm.cache->update_from_translation_gc(dmm.cache, gmv->value->value, target[i].lba, target[i].ppa);	
			}
			
			if(dmm.cache->entry_type==DYNAMIC){
				dmm.cache->update_dynamic_size(dmm.cache, gtd_idx*PAGESIZE/sizeof(uint32_t), gmv->value->value);
			}

			invalidate_ppa(dmm.GTD[gtd_idx].physical_address);
			uint32_t new_ppa=get_map_ppa(gtd_idx);
			dmm.GTD[gtd_idx].physical_address=new_ppa*L2PGAP;
			demand_mapping_inter_write(new_ppa, dmm.li, gmv);
			list_delete_node(temp_list, now);
		}	
	}

	list_free(temp_list);
	return 1;
}

inline uint32_t xx_to_byte(char *a){
	switch(a[0]){
		case 'K':
			return 1024;
		case 'M':
			return 1024*1024;
		case 'G':
			return 1024*1024*1024;
		default:
			break;
	}
	return 1;
}
extern my_cache coarse_cache_func;
extern my_cache fine_cache_func;
extern my_cache sftl_cache_func;

uint32_t demand_argument(int argc, char **argv){
	bool cache_size=false;
	bool cache_type_set=false;
	uint32_t len;
	int c;
	char temp;
	uint32_t gran=1;
	uint64_t base;
	uint32_t physical_page_num;
	double cache_percentage;
	cache_algo_type c_type;
	while((c=getopt(argc,argv,"ctp"))!=-1){
		switch(c){
			case 'c':
				cache_size=true;
				len=strlen(argv[optind]);
				temp=argv[optind][len-1];
				if(temp < '0' || temp >'9'){
					argv[optind][len-1]=0;
					gran=xx_to_byte(&temp);
				}
				base=atoi(argv[optind]);
				physical_page_num=base*gran/PAGESIZE;
				break;
			case 't':
				cache_type_set=true;
				c_type=(cache_algo_type)atoi(argv[optind]);
				dmm.c_type=c_type;
				break;
			case 'p':
				cache_size=true;
				cache_percentage=atof(argv[optind])/100;
				physical_page_num=(SHOWINGSIZE/K) * cache_percentage;
				physical_page_num/=PAGESIZE;
				break;
			default:
				break;
		}
	}
	
	if(cache_type_set){
		switch(c_type){
			case DEMAND_COARSE:
				dmm.cache=&coarse_cache_func;
				break;
			case DEMAND_FINE:
				dmm.cache=&fine_cache_func;
				break;
			case SFTL:
				dmm.cache=&sftl_cache_func;
				break;
			case TPFTL:
				printf("not implemented!\n");
				abort();
				break;
		}
	}
	else{
		dmm.c_type=SFTL;
		dmm.cache=&sftl_cache_func;
	}

	if(!cache_size){
		//physical_page_num=((SHOWINGSIZE/K)/4)/PAGESIZE;
		physical_page_num=((SHOWINGSIZE/K))/2/PAGESIZE;
	}
	dmm.max_caching_pages=physical_page_num;
	return 1;
}
