#include "demand_mapping.h"
#include "page.h"
#include "gc.h"
#include <stdlib.h>
#include <stdint.h>

demand_map_manager dmm;
extern my_cache coarse_cache_func;

inline static void cpy_keys(uint32_t **des, uint32_t *src){
	(*des)=(uint32_t*)malloc(sizeof(KEYT)*L2PGAP);
	memcpy((*des), src, sizeof(KEYT)*L2PGAP);
}

void demand_map_create(uint32_t total_caching_physical_pages, lower_info *li, blockmanager *bm){
	uint32_t total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	uint32_t total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));

	dmm.max_caching_pages=total_caching_physical_pages;
	dmm.GTD=(GTD_entry*)calloc(total_translation_page_num,sizeof(GTD_entry));
	for(uint32_t i=0; i<total_translation_page_num; i++){
		fdriver_mutex_init(&dmm.GTD[i].lock);
		dmm.GTD[i].pending_req=list_init();
	}

	dmm.li=li;
	dmm.bm=bm;
	dmm.reserve=bm->pt_get_segment(bm, MAP_S, true);
	dmm.active=bm->pt_get_segment(bm, MAP_S, false);

	dmm.cache=&coarse_cache_func;
	dmm.cache->init(dmm.cache, total_caching_physical_pages);
}

void demand_map_free(){
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
		demand_mapping_read(etr->physical_address, dmm.li, req, params);
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
	send_user_req(req, DATAR, ppa, req->value);
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

		if(treq->type==FS_SET_T){
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
	KEYT* lba;
	KEYT* physical;
	demand_params *dp;
	assign_params_ex *ap;
	pick_params_ex *pp;
	uint32_t old_ppa;
	uint8_t i;

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

			dmm.cache->insert_entry_from_translation(dmm.cache, etr, lba[i], value);
			__demand_map_pending_read(treq, dp, pp);

			list_delete_node(etr->pending_req, now);
		}
	}

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
			dmm.cache->update_eviction_target_translation(dmm.cache, &dmm.GTD[GETGTDIDX(dp->et.mapping->lba)], value);
		}

		if(treq->type==FS_SET_T){
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
				treq->params=NULL;

				treq->end_req(treq);
				if(req==treq){
					res=1;	
				}
			}
			else{
				if(req==treq){
					list_delete_node(etr->pending_req, now);
					continue;
				}
				dp->status=NONE;
				ap->idx=i;
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
	fdriver_unlock(&etr->lock);
	return 1;
}

uint32_t demand_map_assign(request *req, KEYT *_lba, KEYT *_physical){
	uint8_t i=0;
	demand_params *dp;
	assign_params_ex *mp;

	uint32_t gtd_idx;
	uint32_t trans_offset;
	GTD_entry *etr;
	uint32_t old_ppa;
	KEYT *lba, *physical;

	if(!req->params){
		dp=(demand_params*)malloc(sizeof(demand_params));
		dp->status=NONE;
		gtd_idx=GETGTDIDX(lba[i]);
		trans_offset=TRANSOFFSET(lba[i]);
		etr=dp->etr=&dmm.GTD[gtd_idx];

		mp=(assign_params_ex*)malloc(sizeof(assign_params_ex));
		cpy_keys(&mp->lba,_lba);
		cpy_keys(&mp->physical, _physical);
		i=mp->idx=0;
		dp->params_ex=(void*)mp;

		req->params=(void*)dp;

		lba=_lba;
		physical=_physical;
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
				return map_read_wrapper(etr, req, dmm.li, (void*)dp);
			case NONE:
				if(!dmm.cache->exist(dmm.cache, lba[i])){
					if(dmm.cache->is_needed_eviction(dmm.cache)){
						dp->status=EVICTIONR; //it is dirty
						if(dmm.cache->type==COARSE){
							dp->et.gtd=dmm.cache->get_eviction_GTD_entry(dmm.cache);
							if(dp->et.gtd->physical_address==UINT32_MAX){
								goto retry;						
							}
						}
						else{
							dp->et.mapping=dmm.cache->get_eviction_mapping_entry(dmm.cache);
							target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
							return map_read_wrapper(target_etr, req, dmm.li,(void*)dp);
						}
					}
					if(etr->status!=EMPTY){
						dp->status=MISSR;
						if(dmm.cache->type==COARSE){
							return map_read_wrapper(etr, req, dmm.li, (void*)dp);
						}
					}
				}
				dp->status=HIT;
			case HIT:
				old_ppa=dmm.cache->update_entry(dmm.cache, etr, target->lba, target->ppa);
				if(old_ppa!=UINT32_MAX){
					invalidate_ppa(old_ppa);
				}
				break;
			case EVICTIONR:
				dp->status=EVICTIONW;
				if(dmm.cache->type==COARSE){
					dmm.cache->update_eviction_target_translation(dmm.cache, dp->et.gtd, req->value->value);
					target_etr=dp->et.gtd;
				}
				else{
					demand_map_fine_type_pending(req, dp->et.mapping, req->value->value);
					target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
				}
				if(target_etr->physical_address!=UINT32_MAX)
					invalidate_ppa(target_etr->physical_address);
				target_etr->physical_address=get_map_ppa(gtd_idx);
				demand_mapping_write(target_etr->physical_address, dmm.li, req, (void*)dp);
				return 1;
			case MISSR:
				if(dmm.cache->type==FINE &&(demand_map_fine_type_pending(req, dp->et.mapping, req->value->value)==1)){
					return 1;
				}
				else if(demand_map_coarse_type_pending(req, etr, req->value->value)==1){
					return 1;
				}
				break;
		}
	}
	
	if(i!=L2PGAP){
		return 0;
	}
	if(i==L2PGAP){
		if(req->params){
			free(lba);
			free(physical);
			free(req->params);
		}
		req->end_req(req);
		return 1;
	}
	return 0;
}


uint32_t demand_page_read(request *const req){
	demand_params *dp;
	uint32_t gtd_idx, trans_offset;
	uint32_t ppa;
	GTD_entry *etr;

	if(!req->params){
		dp=(demand_params*)malloc(sizeof(demand_params));
		dp->status=NONE;
		gtd_idx=GETGTDIDX(req->key);
		trans_offset=TRANSOFFSET(req->key);
		etr=dp->etr=&dmm.GTD[gtd_idx];
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
			if(etr->status==EMPTY){
				printf("%s:%d non populate error!\n", __FILE__,__LINE__);
				abort();
			}
			/*if(demand_check_pending_mapping(req, etr, &ppa)){
				goto read_data;
			}*/
			if(!dmm.cache->exist(dmm.cache, req->key)){//cache miss
				if(dmm.cache->is_needed_eviction(dmm.cache)){
					if(dmm.cache->type==COARSE){
						dp->et.gtd=dmm.cache->get_eviction_GTD_entry(dmm.cache);
						dp->status=EVICTIONR; //it is dirty
						if(dp->et.gtd->physical_address==UINT32_MAX){
							goto retry;						
						}
					}
					else{
						dp->et.mapping=dmm.cache->get_eviction_mapping_entry(dmm.cache);
						target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
						return map_read_wrapper(target_etr, req, dmm.li,(void*)dp);
					}
					dp->status=MISSR;
					return map_read_wrapper(etr, req, dmm.li, (void*)dp);
				}
			}
			dp->status=HIT;
		case HIT:
			ppa=dmm.cache->get_mapping(dmm.cache, req->key);
			goto read_data;
		case EVICTIONR:
			dp->status=EVICTIONW;
			if(dmm.cache->type==COARSE){
				dmm.cache->update_eviction_target_translation(dmm.cache, dp->et.gtd, req->value->value);
				target_etr=dp->et.gtd;
			}
			else{
				demand_map_fine_type_pending(req, dp->et.mapping, req->value->value);
				target_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
			}
			if(target_etr->physical_address!=UINT32_MAX)
				invalidate_ppa(target_etr->physical_address);
			target_etr->physical_address=get_map_ppa(gtd_idx);
			demand_mapping_write(target_etr->physical_address, dmm.li, req, dp);
			return 1;
		case MISSR:
			if(dmm.cache->type==FINE &&(demand_map_fine_type_pending(req, dp->et.mapping, req->value->value)==1)){
				goto end;
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
	send_user_req(req, DATAR, ppa, req->value);
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
	for(uint32_t i=0; i<idx; i++){
		temp_gtd_idx=GETGTDIDX(target[i].lba);
		if(dmm.cache->exist(dmm.cache, target[i].lba)){
			dmm.cache->update_entry_gc(dmm.cache, &dmm.GTD[temp_gtd_idx], target[i].lba, target[i].ppa);
		}else{
			if(read_start && gtd_idx==temp_gtd_idx) continue;
			if(!read_start){read_start=true;}

			gmv=(gc_map_value*)malloc(sizeof(gc_map_value));
			gmv->pair=target[i];
			gmv->start_idx=i;
			gmv->gtd_idx=gtd_idx;
			gmv->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			gmv->isdone=false;
			list_insert(temp_list, (void*)gmv);
			demand_mapping_inter_read(dmm.GTD[gtd_idx].physical_address, dmm.li, gmv);
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
			
			invalidate_ppa(dmm.GTD[gtd_idx].physical_address);
			uint32_t new_ppa=get_map_ppa(gtd_idx);
			dmm.GTD[gtd_idx].physical_address=new_ppa;
			demand_mapping_inter_write(new_ppa, dmm.li, gmv);
		}	
	}
	return 1;
}
