#include "demand_mapping.h"
#include "page.h"
#include "gc.h"
#include <stdlib.h>
#include <getopt.h>
#include <stdint.h>
#include <unistd.h>
#include "../../include/utils/data_copy.h"
extern uint32_t test_key;
extern uint32_t test_ppa;
extern uint32_t debug_lba;
extern algorithm demand_ftl;
extern my_cache coarse_cache_func;
extern my_cache fine_cache_func;
extern my_cache sftl_cache_func;
extern my_cache nocpu_sftl_cache_func;
extern my_cache tp_cache_func;
demand_map_manager dmm;
dmi DMI;

#include "./caching/sftl/sftl_cache.h"
extern sftl_cache_monitor scm;
fdriver_lock_t dmi_lock;

extern algorithm demand_ftl;
uint32_t cache_traverse_state(request *req, mapping_entry *now_pair, demand_param *dp, 
		uint32_t *prefetching_info, bool iswrite_path);
inline static void cpy_keys(uint32_t **des, uint32_t *src){
	(*des)=(uint32_t*)malloc(sizeof(KEYT)*L2PGAP);
	memcpy((*des), src, sizeof(KEYT)*L2PGAP);
}

pthread_mutex_t traverse_lock=PTHREAD_MUTEX_INITIALIZER;

static void demand_cache_log(){
	printf("===========cache results========\n");
	printf("Cache miss num: %u\n",DMI.miss_num);
	printf("\tCache cold miss num: %u\n",DMI.write_cold_miss_num+DMI.read_cold_miss_num);
	printf("\tCache capacity miss num: %u\n",DMI.miss_num - (DMI.write_cold_miss_num+DMI.read_cold_miss_num));

	printf("\tread miss num: %u\n", DMI.read_miss_num);
	printf("\t\tread_cold_miss_num:%u\n", DMI.read_cold_miss_num);
	printf("\t\tread_capacity_miss_num:%u\n", DMI.read_miss_num-DMI.read_cold_miss_num);

	printf("\twrite miss num: %u\n", DMI.write_miss_num);
	printf("\t\twrite_cold_miss_num:%u\n", DMI.write_cold_miss_num);
	printf("\t\twrite_capacity_miss_num:%u\n", DMI.write_miss_num-DMI.write_cold_miss_num);

	printf("Cache hit num: %u\n",DMI.hit_num);
	printf("\tread hit num: %u\n", DMI.read_hit_num);
	printf("\t\tread shadow hit num: %u\n", DMI.read_shadow_hit_num);
	printf("\twrite hit num: %u\n", DMI.write_hit_num);
	printf("\t\twrite shadow hit num: %u\n", DMI.write_shadow_hit_num);
	printf("Cache hit ratio: %.2lf%%\n", (double) DMI.hit_num / (DMI.miss_num+DMI.hit_num) * 100);

	printf("\nCache eviction cnt: %u\n", DMI.eviction_cnt);
	printf("\tHit eviction: %u\n", DMI.hit_eviction);
	printf("\tadditional_eviction:%u", DMI.additional_eviction_cnt);
	printf("\tEviction clean cnt: %u\n", DMI.clean_eviction);
	printf("\tEviction dirty cnt: %u\n", DMI.dirty_eviction);
	printf("===============================\n");

	printf("write_working_set:%lf\n", (double)DMI.write_working_set_num/RANGE);
	printf("read_working_set:%lf\n", (double)DMI.read_working_set_num/RANGE);

	fdriver_lock(&dmi_lock);
	bitmap_free(DMI.read_working_set);
	bitmap_free(DMI.write_working_set);

	memset(&DMI, 0, sizeof(DMI));
	
	DMI.read_working_set=bitmap_init(RANGE);
	DMI.write_working_set=bitmap_init(RANGE);
	fdriver_unlock(&dmi_lock);
}

static inline char *cache_traverse_type(MAP_ASSIGN_STATUS a){
	switch(a){
		case NONE: return "NONE";
		case HIT:return "HIT";
		case EVICTIONW: return "EVICTIONW";
		case EVICTIONR: return "EVICTIONR";
		case MISSR: return "MISSR";
	}
	return NULL;
}


static inline void reinsert_stopped_request(){
	std::list<request*>::iterator it=dmm.stopped_request->begin();
	for(;it!=dmm.stopped_request->end();){
		if(!inf_assign_try(*it)){
			EPRINT("queue full?", true);
		}
		dmm.stopped_request->erase(it++);
	}
}

enum{
	M_WRITE, M_READ, M_DELAY
};


uint32_t demand_print_log(){
	printf("FTL-LOG\n");
	if(dmm.cache->print_log){
		dmm.cache->print_log(dmm.cache);
	}
	demand_cache_log();
	printf("FTL-LOG-END\n");
	return 1;
}

static inline void dp_monitor_update(demand_param *dp, bool iswrite, uint32_t type){
	switch(type){
		case M_WRITE:
			iswrite?dp->write_w_num++:dp->read_w_num++;
			break;
		case M_READ:
			iswrite?dp->write_r_num++:dp->read_r_num++;
			break;
		case M_DELAY:
			iswrite?dp->write_d_num++:dp->read_d_num++;
			break;
	}
}

static inline void demand_req_data_collect(demand_param *dp, bool iswrite, bool finish){
#ifdef CHECKING_TEST_TIME
	measure_calc(&dp->mt);
#endif
	dp->read_d_num=dp->read_d_num>10?10:dp->read_d_num;
	dp->write_d_num=dp->write_d_num>10?10:dp->write_d_num;
	if(iswrite){
		if(dp->is_hit_eviction){
			printf("hit_eviction:%u\t%u\t%u\n", dp->write_w_num, dp->write_r_num, dp->write_d_num);
		}
		DMI.write_cnt++;
#ifdef CHECKING_TEST_TIME
		uint64_t temp=DMI.write_time[dp->write_w_num][dp->write_r_num][dp->write_d_num];
		uint64_t cnt=DMI.write_data[dp->write_w_num][dp->write_r_num][dp->write_d_num];
		temp=(temp*cnt+dp->mt.micro_time)/(cnt+1);
		DMI.write_time[dp->write_w_num][dp->write_r_num][dp->write_d_num]=temp;
#endif	
		DMI.write_data[dp->write_w_num][dp->write_r_num][dp->write_d_num]++;
		dp->write_w_num=dp->write_r_num=dp->write_d_num=0;
	}
	else{
		DMI.read_cnt++;
#ifdef CHECKING_TEST_TIME
		uint64_t temp=DMI.read_time[dp->read_w_num][dp->read_r_num][dp->read_d_num];
		uint64_t cnt=DMI.read_data[dp->read_w_num][dp->read_r_num][dp->read_d_num];
		temp=(temp*cnt+dp->mt.micro_time)/(cnt+1);
		DMI.read_time[dp->read_w_num][dp->read_r_num][dp->read_d_num]=temp;
#endif	

		DMI.read_data[dp->read_w_num][dp->read_r_num][dp->read_d_num]++;
		dp->read_w_num=dp->read_r_num=dp->read_d_num=0;
	}
#ifdef CHECKING_TEST_TIME
	if(!finish){
		measure_start(&dp->mt);
	}
#endif
}


static void write_done_check(request *req){
	if(req->type==FS_DELETE_T){
		req->end_req(req);
	//	printf("done %u\n", req->global_seq);
		return;
	}
	bool should_end_req=false;
	fdriver_lock(&req->done_lock);
	req->map_done=true;
	should_end_req=req->map_done && req->write_done;
	fdriver_unlock(&req->done_lock);
	if(should_end_req){
		fdriver_destroy(&req->done_lock);
		req->end_req(req);
	}
}

void print_all_processed_req(){
	std::map<uint32_t, request *>::iterator iter;
	for(iter=dmm.all_now_req->begin(); iter!=dmm.all_now_req->end(); iter++){
		demand_param *dp=(demand_param*)iter->second->param;
		assign_param_ex* mp=(assign_param_ex*)dp->param_ex;
		if(iter->second->type==1){
			printf("[%s] req->type:%u seq:%u now_eviction_hint:%u lba:%u\n", cache_traverse_type(dp->status),
					iter->second->type, iter->second->global_seq, dp->now_eviction_hint,
					mp->lba[mp->idx]);
		}
		else{
			printf("[%s] req->type:%u seq:%u now_eviction_hint%u lba:%u\n", cache_traverse_type(dp->status),
					iter->second->type, iter->second->global_seq, dp->now_eviction_hint,
					iter->second->key);	
		}
	}
	printf("total eviction hint:%u, map_cnt:%lu\n", dmm.eviction_hint, dmm.all_now_req->size());
}

uint32_t all_eviction_hint(){
	uint32_t res=0;
	std::map<uint32_t, request *>::iterator iter;
	for(iter=dmm.all_now_req->begin(); iter!=dmm.all_now_req->end(); iter++){
		demand_param *dp=(demand_param*)iter->second->param;
		res+=dp->now_eviction_hint;
	}
	return res;
}


static inline char *cache_type(cache_algo_type t){
	switch(t){
		case DEMAND_COARSE: return "DEMAND_COARSE";
		case DEMAND_FINE: return "DEMAND_FINE";
		case SFTL: return "SFTL";
		case TPFTL: return "TPFTL";
		case NOCPU_SFTL: return "NOCPU_SFTL";
		default:
			abort();
	}
}

static inline void mapping_sanity_checker_with_cache(char *value, uint32_t etr_idx){
#ifndef DFTL_DEBUG
	return;
#endif
	uint32_t *map=(uint32_t*)value;
	for(uint32_t i=0; i<PAGESIZE/sizeof(uint32_t); i++){
		if(map[i]!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm, map[i])){
			//uint32_t lba=((uint32_t*)demand_ftl.bm->get_oob(demand_ftl.bm, map[i]/L2PGAP))[map[i]%L2PGAP];
			uint32_t lba=GTD_IDX_TO_FIRST_LBA(etr_idx)+i;
			if(!dmm.cache->exist(dmm.cache, lba)){
				printf("%u %u mapping sanity error\n", lba, map[i]);
				abort();
			}
		}
	}
}

static inline void dp_prev_init(demand_param *dp){
	memset(dp->prev_status, 0, sizeof(dp->prev_status));
	dp->log=0;
}

static inline void dp_status_update(demand_param *dp, MAP_ASSIGN_STATUS status){
	if(status==NONE){
		dp->log=0;
		memset(dp->prev_status, 0, sizeof(dp->prev_status));
	}

	if(dp->status==status && status!=NONE){
		if(!dp->is_hit_eviction){
			abort();
		}
	}

	if(dp->prev_status[dp->log]==dp->status && dp->status!=NONE && dp->status!=HIT){
		abort();
	}

	dp->prev_status[dp->log++]=dp->status;
	dp->status=status;
}

static char* get_demand_status_name(MAP_ASSIGN_STATUS a){
	switch(a){
		case NONE: return "NONE";
		case HIT: return "HIT";
		case EVICTIONW: return "EVICTIONW";
		case EVICTIONR: return "EVICTIONR";
		case MISSR: return "MISSR";
		default: return "";
	}
	return "";
}

static inline void update_cache_entry_wrapper(GTD_entry *target_etr, uint32_t lba, uint32_t ppa, bool ispending){
	uint32_t old_ppa=dmm.cache->update_entry(dmm.cache, target_etr, lba, ppa, &dmm.eviction_hint);
	if(old_ppa!=UINT32_MAX){
#ifdef DFTL_DEBUG
		if(old_ppa==ppa){
			printf("updating same ppa %u %u\n", lba, ppa);
			abort();
		}
		if(dmm.cache->type==COARSE && !dmm.GTD[GETGTDIDX(lba)].private_data){
			printf("????\n");
			abort();
		}
		if(ispending){
			printf("MISSR - lba:%u ppa:%u -> %u, read_mapping:%u\n", lba, old_ppa, ppa, dmm.cache->get_mapping(dmm.cache, lba));
		}
		else{
			printf("HIT - lba:%u ppa:%u -> %u, read_mapping:%u\n", lba, old_ppa, ppa, dmm.cache->get_mapping(dmm.cache, lba));
		}
#endif
		if(!demand_ftl.bm->bit_query(demand_ftl.bm, old_ppa)){
			uint32_t r_lba=((uint32_t*)demand_ftl.bm->get_oob(demand_ftl.bm, old_ppa/L2PGAP))[old_ppa%L2PGAP];
			if(r_lba!=lba){
				printf("????\n");
			}
			if(r_lba!=UINT32_MAX){
				printf("%u is in mapp??\n", old_ppa);
			}
			else if(r_lba==UINT32_MAX){
				printf("already gced entry\n");
			}
		}
		invalidate_ppa(old_ppa);
	}
}


void demand_map_create_body(uint32_t total_caching_physical_pages, lower_info *li, blockmanager *bm, bool data_load){
	uint32_t total_logical_page_num;
	uint32_t total_translation_page_num;

	total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));

	if(total_caching_physical_pages!=UINT32_MAX){
		dmm.max_caching_pages=total_caching_physical_pages;
	}
	else{

	}

	fdriver_mutex_init(&dmm.flying_map_read_lock);
	dmm.flying_map_read_req_set=new std::map<uint32_t, request*>();
	dmm.flying_map_read_flag_set=new std::map<uint32_t, bool>();
	dmm.flying_req=new std::map<uint32_t, request*>();

	dmm.all_now_req=new std::map<uint32_t, request*>();
	dmm.stopped_request=new std::list<request *>();

	dmm.li=li;
	dmm.bm=bm;


	printf("--------------------\n");
	printf("|DFTL settings\n");
	
	printf("|cache type: %s\n", cache_type(dmm.c_type));
	printf("|cache size: %.2lf(mb)\n", ((double)dmm.max_caching_pages * PAGESIZE)/_M);
	printf("|\tratio of PFTL: %.2lf%%\n", ((double)dmm.max_caching_pages * PAGESIZE)/(SHOWINGSIZE/_K)*100);

	uint32_t cached_entry=dmm.cache->init(dmm.cache, dmm.max_caching_pages);
	printf("|\tcaching percentage: %.2lf%%\n", (double)cached_entry/total_logical_page_num *100);
	printf("--------------------\n");
	if(!data_load){
		DMI.read_working_set=bitmap_init(RANGE);
		DMI.read_working_set_num=0;
		DMI.write_working_set=bitmap_init(RANGE);
		DMI.write_working_set_num=0;
	}
	fdriver_mutex_init(&dmi_lock);
}

void demand_map_create(uint32_t total_caching_physical_pages, lower_info *li, blockmanager *bm){
	demand_map_create_body(total_caching_physical_pages, li, bm, false);

	uint32_t total_logical_page_num;
	uint32_t total_translation_page_num;
	total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));
	dmm.GTD=(GTD_entry*)calloc(total_translation_page_num,sizeof(GTD_entry));
	for(uint32_t i=0; i<total_translation_page_num; i++){
		fdriver_mutex_init(&dmm.GTD[i].lock);
		dmm.GTD[i].idx=i;
		dmm.GTD[i].pending_req=list_init();
		dmm.GTD[i].physical_address=UINT32_MAX;
	}
}

void demand_map_free(){
	demand_cache_log();

	printf("write collected data\n");
	printf("W_NUM\tR_NUM\tD_NUM\tCNT\t\tRATIO\tA_TIME\n");
	for(uint32_t i=0; i<10; i++){
		for(uint32_t j=0; j<10; j++){
			for(uint32_t k=0; k<10; k++){
				if(DMI.write_data[i][j][k]){
					printf("%u\t%u\t%u\t%u\t\t%.2f\t%lu\n", i, j, k, DMI.write_data[i][j][k], (float)DMI.write_data[i][j][k]/DMI.write_cnt,
							DMI.write_time[i][j][k]);
				}
			}
		}
	}
	printf("\nread collected data\n");
	printf("W_NUM\tR_NUM\tD_NUM\tCNT\t\tRATIO\tA_TIME\n");
	for(uint32_t i=0; i<10; i++){
		for(uint32_t j=0; j<10; j++){
			for(uint32_t k=0; k<10; k++){
				if(DMI.read_data[i][j][k]){
					printf("%u\t%u\t%u\t%u\t\t%.2f\t%lu\n", i, j, k, DMI.read_data[i][j][k], (float)DMI.read_data[i][j][k]/DMI.read_cnt,
							DMI.read_time[i][j][k]);
				}
			}
		}
	}

	uint32_t total_logical_page_num=(SHOWINGSIZE/LPAGESIZE);
	uint32_t total_translation_page_num=total_logical_page_num/(PAGESIZE/sizeof(DMF));

	printf("----- traffic result -----\n");


	for(uint32_t i=0; i<total_translation_page_num; i++){
		fdriver_destroy(&dmm.GTD[i].lock);
		list_free(dmm.GTD[i].pending_req);
	}
	dmm.cache->free(dmm.cache);
	delete dmm.flying_map_read_req_set;
	delete dmm.flying_map_read_flag_set;
	delete dmm.flying_req;
	free(dmm.GTD);
}

uint32_t pending_debug_seq;

static inline void notfound_processing(request *req){
	demand_param *dp=(demand_param*)req->param;
	assign_param_ex *mp=(assign_param_ex*)dp->param_ex;
	free(mp->prefetching_info);
	free(mp);
	req->type=FS_NOTFOUND_T;

	demand_req_data_collect(dp, req->type==FS_SET_T, true);
	free(dp);
	req->end_req(req);
}

static inline void dp_initialize(demand_param *dp){
	dp_status_update(dp, NONE);
	dp_prev_init(dp);
	dp->now_eviction_hint=0;
	dp->is_hit_eviction=false;
	dp->is_fresh_miss=false;
}

enum{
	EVICTION_READ, COLD_MISS_READ, CAP_MISS_READ, AFTER_EVICTION
};

static uint32_t get_mapping_wrapper(request *req, uint32_t lba){
#ifdef MAPPING_TIME_CHECK
	if (req->mapping_cpu_check){
		measure_start(&req->mapping_cpu);
	}
#endif
	uint32_t res=dmm.cache->get_mapping(dmm.cache, lba);
#ifdef MAPPING_TIME_CHECK
	if (req->mapping_cpu_check){
		measure_adding(&req->mapping_cpu);
	}
#endif
	return res;
}

uint32_t map_read_wrapper(GTD_entry *etr, request *req, lower_info *, demand_param *param, 
		uint32_t target_data_lba, uint32_t type){
	param->flying_map_read_key=target_data_lba;
	if(req->type==FS_GET_T){
		if(etr->physical_address==UINT32_MAX){
			return NOTFOUND_END;
		}
	}

	if(etr->idx!=GETGTDIDX(target_data_lba)){
		abort();
	}

	req->type_ftl|=1;
	if(etr->status==FLYING){
		switch(type){
			case EVICTION_READ:
				DMI.eviction_shadow_hit_num++;
				break;
			case COLD_MISS_READ:
				req->type==FS_SET_T?DMI.write_cold_miss_num--:DMI.read_cold_miss_num--;
				req->type==FS_SET_T?DMI.write_shadow_hit_num++:DMI.read_shadow_hit_num++;
				break;
			case AFTER_EVICTION:
			case CAP_MISS_READ:
				req->type==FS_SET_T?DMI.write_shadow_hit_num++:DMI.read_shadow_hit_num++;
				break;
		}

		//assign_param_ex *ap=(assign_param_ex*)param->param_ex;
		//printf("overlap %u gtd idx:%u, input_lba:%u target_lba:%u\n",req->seq, etr->idx, target_data_lba, ap->lba[ap->idx]);
		pending_debug_seq=req->seq;
		fdriver_lock(&etr->lock);
		list_insert(etr->pending_req, (void*)req);
		fdriver_unlock(&etr->lock);
		return FLYING_HIT_END;
	}else{
		fdriver_lock(&etr->lock);
		fdriver_lock(&dmm.flying_map_read_lock);
		std::map<uint32_t, request*>::iterator iter=dmm.flying_map_read_req_set->find(GETGTDIDX(target_data_lba));
		if(iter!=dmm.flying_map_read_req_set->end()){
			printf("%u overlap key should not be inserted!\n", iter->first);
			abort();
		}
		else{
			//printf("%u is flying inserted\n", GETGTDIDX(target_data_lba));
		}

		dmm.flying_map_read_req_set->insert(std::pair<uint32_t, request*>(GETGTDIDX(target_data_lba), req));
		dmm.flying_map_read_flag_set->insert(std::pair<uint32_t, bool>(GETGTDIDX(target_data_lba), false));

		fdriver_unlock(&dmm.flying_map_read_lock);

		demand_mapping_read(etr->physical_address/L2PGAP, dmm.li, req, param);
		list_insert(etr->pending_req, (void*)req);
	
		bool all_none=true;
		for(uint32_t i=0; i<10; i++){
			if(param->prev_status[i]!=NONE){
				all_none=false;
				break;
			}
		}

		etr->status=FLYING;
		fdriver_unlock(&etr->lock);

		dp_monitor_update(param, req->type==FS_SET_T, M_READ);
		return MAP_READ_ISSUE_END;
	}
}

static inline void write_updated_map(request *req, GTD_entry *target_etr, 
		demand_param *dp){
	uint32_t temp=target_etr->physical_address;
	if(temp!=UINT32_MAX){
		invalidate_map_ppa(temp);
	}
	bool is_map_gc_triggered=false;
	target_etr->physical_address=get_map_ppa(target_etr->idx, &is_map_gc_triggered)*L2PGAP;

	if(is_map_gc_triggered){
		req->type_ftl|=4;
	}
	mapping_sanity_checker_with_cache(req->value->value, target_etr->idx);
#ifdef DFTL_DEBUG
	printf("entry write:%u:%u\n\n",target_etr->idx, target_etr->physical_address);
#endif

	dp_monitor_update(dp, req->type==FS_SET_T, M_WRITE);
	req->type_ftl|=2;
	demand_mapping_write(target_etr->physical_address/L2PGAP, dmm.li, req, (void*)dp);
}

inline void __demand_map_pending_read(request *req, demand_param *dp, bool need_retrieve){
	if(need_retrieve){
		dp->target.ppa=get_mapping_wrapper(req, dp->target.lba);
	}

	//printf("[de2] req->global_seq:%u\n", req->global_seq);
	dmm.all_now_req->erase(req->global_seq);
	//debug_size();

	if(dp->target.ppa==UINT32_MAX){
		//printf("try to read invalidate ppa %s:%d\n", __FILE__,__LINE__);
		notfound_processing(req);
		return;
	}

	if(req->key==debug_lba){
		printf("%u read ppa %u\n", req->key, dp->target.ppa);
	}

	req->value->ppa=dp->target.ppa;
	demand_req_data_collect(dp, req->type==FS_SET_T, true);
	send_user_req(req, DATAR, dp->target.ppa/L2PGAP, req->value);
	if(dp->param_ex){
		assign_param_ex *mp=(assign_param_ex*)dp->param_ex;
		free(mp->prefetching_info);
		free(mp);
	}
	free(dp);
}


bool flying_request_hit_check(request *req, uint32_t lba, demand_param *dp ,bool iswrite){
	GTD_entry *etr=&dmm.GTD[GETGTDIDX(lba)];
	fdriver_lock(&etr->lock);
	if(dmm.cache->type==COARSE && etr->status==FLYING){
		dp_status_update(dp, MISSR);
		list_insert(etr->pending_req, (void*)req);
		dp->now_eviction_hint=0;
		fdriver_unlock(&etr->lock);
		return true;
	}
	fdriver_unlock(&etr->lock);
	return false;
}

uint32_t demand_map_coarse_type_pending(request *req, GTD_entry *etr, char *value){
	list_node *now, *next;
	request *treq;
	KEYT* lba;
	KEYT* physical;
	demand_param *dp=(demand_param*)req->param;
	assign_param_ex *ap;
	uint32_t flying_map_read_lba=dp->flying_map_read_key;
	uint32_t i;

	uint32_t res=0;
	dmm.cache->insert_entry_from_translation(dmm.cache, etr, UINT32_MAX, value, &dmm.eviction_hint, dp->now_eviction_hint);
	dp->now_eviction_hint=0;

	if((int)dmm.eviction_hint<0){
		printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
		abort();
	}

	fdriver_lock(&etr->lock);
	for_each_list_node_safe(etr->pending_req, now, next){
		treq=(request*)now->data;
		dp=(demand_param*)treq->param;
		dp->flying_map_read_key=UINT32_MAX;
		ap=NULL;

		if((treq->type==FS_SET_T|| treq->type==FS_DELETE_T) && dp->status==MISSR){
			ap=(assign_param_ex*)dp->param_ex;
			lba=ap->lba;
			physical=ap->physical;
			i=ap->idx;
			if(etr->idx!=GETGTDIDX(lba[i])){
				printf("%u is differ ppa %u - max_idx:%u, req->seq:%u\n", lba[i], physical[i], ap->max_idx, treq->seq);
			}

			if(dmm.cache->entry_type==DYNAMIC){
				static int cnt=0;
				uint32_t temp_size=dmm.eviction_hint-dp->now_eviction_hint;
				if(dmm.cache->get_remain_space(dmm.cache, temp_size) < dp->now_eviction_hint){
					printf("size error!, is it really problem??:%u\n", cnt++);
					//abort();
				}
			}
			
			update_cache_entry_wrapper(etr, lba[i], physical[i], true);

			if(dmm.cache->entry_type==DYNAMIC){
				dmm.eviction_hint=dmm.cache->update_hit_eviction_hint(dmm.cache, lba[i], NULL, dmm.eviction_hint, &dp->now_eviction_hint, false);
				dp->now_eviction_hint=0;
			}

			i++;
			demand_req_data_collect(dp, treq->type==FS_SET_T, i==ap->max_idx);
			if(i==ap->max_idx){
				dmm.all_now_req->erase(treq->global_seq);
				//debug_size();
				free(lba);
				free(physical);
				if(ap){
					free(ap->prefetching_info);
					free(ap);
				}

				dmm.flying_req->erase(treq->seq);
				free(dp);
				//treq->end_req(treq);
				write_done_check(treq);
				if(req==treq){
					res=1;	
				}
			}
			else{
				ap->idx=i;
				dp_initialize(dp);
				if(req==treq){
					list_delete_node(etr->pending_req, now);
					ap->idx=i;
					continue;
				}

				if(!inf_assign_try(treq)){
					abort();
				}
			}
		}
		else if(treq->type==FS_GET_T){
			if(req==treq){
				res=1;			
			}
			if(dmm.cache->entry_type==DYNAMIC){
				if(dp->now_eviction_hint){
					printf("why???\n");
				}
				dp->now_eviction_hint=0;
			}
			__demand_map_pending_read(treq, dp, true);
		}
		else{
			printf("unknown type! %s:%d\n", __FILE__, __LINE__);
			abort();
		}

		list_delete_node(etr->pending_req, now);
	}

	if(flying_map_read_lba!=UINT32_MAX){
		fdriver_lock(&dmm.flying_map_read_lock);
		std::map<uint32_t, bool>::iterator iter=dmm.flying_map_read_flag_set->find(GETGTDIDX(flying_map_read_lba));
		if(!iter->second){
			printf("????\n");
		}
		dmm.flying_map_read_req_set->erase(GETGTDIDX(flying_map_read_lba));
		dmm.flying_map_read_flag_set->erase(GETGTDIDX(flying_map_read_lba));
		fdriver_unlock(&dmm.flying_map_read_lock);
	}

	fdriver_unlock(&etr->lock);
	return res;
}

uint32_t demand_map_fine_type_pending(request *const req, mapping_entry *mapping, char *, uint32_t *sequential_cnt){
	list_node *now, *next;
	request *treq;
	KEYT* lba=NULL;
	KEYT* physical=NULL;
	demand_param *dp=(demand_param*)req->param;
	uint32_t flying_map_read_lba=dp->flying_map_read_key;
	assign_param_ex *ap;
	uint32_t i=0;

	GTD_entry *etr=&dmm.GTD[GETGTDIDX(mapping->lba)];
	uint32_t head_req_is_missread=0;

	bool pending_isupdated_mapping=false;
	request *pending_eviction_req[QDEPTH];
	request *eviction_last_req=NULL;
	uint32_t pending_eviction_req_idx=0;
	uint32_t head_request_finished=0;
	bool already_in_cache;
	bool is_update_mapping=false;
	bool value_free=false;
	char *temp_value=NULL;

	fdriver_lock(&etr->lock);

	if(etr->pending_req->size > 1){
		value_free=true;
		temp_value=(char*)malloc(PAGESIZE);
		data_copy(temp_value, req->value->value, PAGESIZE);
	}
	else{
		temp_value=req->value->value;
	}
	if(etr->physical_address!=UINT32_MAX){
		mapping_sanity_checker_with_cache(temp_value, etr->idx);
	}

	etr->status=POPULATE;
	for_each_list_node_safe(etr->pending_req, now, next){
		treq=(request*)now->data;
		dp=(demand_param*)treq->param;
		dp->flying_map_read_key=UINT32_MAX;
		ap=(assign_param_ex*)dp->param_ex;

		mapping=&dp->target;
		sequential_cnt=&ap->prefetching_info[ap->idx];


		if(dp->status==MISSR){
			if(treq==req){
				head_req_is_missread=1;
			}
			uint32_t temp_ppa=((uint32_t*)temp_value)[mapping->lba%(PAGESIZE/sizeof(uint32_t))];
			already_in_cache=dmm.cache->insert_entry_from_translation(dmm.cache, etr, mapping->lba, temp_value, 
					&dmm.eviction_hint, dp->now_eviction_hint);
			if(!already_in_cache && temp_ppa!=UINT32_MAX && !demand_ftl.bm->bit_query(demand_ftl.bm, temp_ppa)){
				printf("wtf %u %u gtd-ppa:%u:%u\n", mapping->lba, temp_ppa,etr->idx, etr->physical_address);
				abort();
			}

			if(treq->type==FS_SET_T || treq->type==FS_DELETE_T){
				ap=(assign_param_ex*)dp->param_ex;
				lba=ap->lba;
				physical=ap->physical;
				i=ap->idx;
				update_cache_entry_wrapper(etr, lba[i], physical[i], true);

				i++;
				demand_req_data_collect(dp, treq->type==FS_SET_T, i==ap->max_idx);
				if(i==ap->max_idx){
					dmm.all_now_req->erase(treq->global_seq);
					//debug_size();
					free(lba);
					free(physical);
					if(ap){
						free(ap->prefetching_info);
						free(ap);
					}
					if(treq==req) {
						head_request_finished=1;
					}

					dmm.flying_req->erase(treq->seq);
					//send_user_req(treq, DATAW, dp->ppa, dp->value);
					free(dp);
//					treq->end_req(treq);
					write_done_check(treq);
				}
				else{
					dp_initialize(dp);
					ap->idx=i;
					if(req==treq){
						list_delete_node(etr->pending_req, now);
						ap->idx=i;
						continue;
					}

					if(!inf_assign_try(treq)){
						abort();
					}
				}
			}
			else{
				if(treq==req){
					head_request_finished=1;
				}
				__demand_map_pending_read(treq, dp, true);
			}
			list_delete_node(etr->pending_req, now);
		}
		else if(dp->status==EVICTIONR){
			if(!is_update_mapping){
				dmm.cache->update_eviction_target_translation(dmm.cache, treq->key, etr, dp->et.mapping, temp_value, dp->cache_private, true);
				is_update_mapping=true;
			}
			else{
				dmm.cache->evict_target(dmm.cache, NULL, dp->et.mapping);
			}

			if(req==treq){
				list_delete_node(etr->pending_req, now);
				continue;
			}
			else{
				pending_isupdated_mapping=true;
				dp_status_update(dp, EVICTIONW);
				pending_eviction_req[pending_eviction_req_idx++]=treq;
				eviction_last_req=treq;
			}
			list_delete_node(etr->pending_req, now);
		}
		else{
			printf("mapping lba:%u ppa:%u ",mapping->lba, mapping->ppa);
			EPRINT("is it valid state?", true);
		}
	}
	
/*
	if(etr->pending_req->size==0) goto end;
	for_each_list_node_safe(etr->pending_req, now, next){ //for eviction
		treq=(request*)now->data;
		dp=(demand_param*)treq->param;
		dp->flying_map_read_key=UINT32_MAX;
		ap=NULL;

		dmm.cache->update_eviction_target_translation(dmm.cache, treq->key, etr, dp->et.mapping, temp_value, dp->cache_private);
		if(req==treq){
			list_delete_node(etr->pending_req, now);
			continue;
		}
		else{
			dmm.cache->evict_target(dmm.cache, NULL, dp->et.mapping);
			pending_isupdated_mapping=true;
			dp_status_update(dp, EVICTIONW);
			pending_eviction_req[pending_eviction_req_idx++]=treq;
			eviction_last_req=treq;
		}
		list_delete_node(etr->pending_req, now);
	}
*/	
	pending_eviction_req_idx=pending_eviction_req_idx?pending_eviction_req_idx-1:0;
	for(i=0; i<pending_eviction_req_idx; i++){
		dp_monitor_update((demand_param*)pending_eviction_req[i]->param, pending_eviction_req[i]->type==FS_SET_T, M_DELAY);
		if(!inf_assign_try(pending_eviction_req[i])){
			abort();
		}
	}

	if(head_req_is_missread && pending_isupdated_mapping){ /*write mapping*/
		data_copy(eviction_last_req->value->value, temp_value, PAGESIZE);
		write_updated_map(eviction_last_req, etr, (demand_param*)eviction_last_req->param);
	}
	else if(pending_isupdated_mapping){
		dp_monitor_update((demand_param*)eviction_last_req->param, eviction_last_req->type==FS_SET_T, M_DELAY);
		if(!inf_assign_try(eviction_last_req)){
			abort();
		}
	}

	if(head_req_is_missread==0 && value_free){
		data_copy(req->value->value, temp_value, PAGESIZE);
	}

//end:
	if(flying_map_read_lba!=UINT32_MAX){
		fdriver_lock(&dmm.flying_map_read_lock);
		std::map<uint32_t, bool>::iterator iter=dmm.flying_map_read_flag_set->find(GETGTDIDX(flying_map_read_lba));
		dmm.flying_map_read_req_set->erase(GETGTDIDX(flying_map_read_lba));
		dmm.flying_map_read_flag_set->erase(GETGTDIDX(flying_map_read_lba));
		fdriver_unlock(&dmm.flying_map_read_lock);
	}

	fdriver_unlock(&etr->lock);

	if(value_free){
		free(temp_value);
	}
	return head_request_finished;
}


static inline uint32_t updating_mapping_for_eviction(request *req, demand_param *dp, mapping_entry *target, uint32_t *prefetching_info){
	GTD_entry *eviction_etr=NULL;
	if(dmm.cache->type==COARSE){
		eviction_etr=dp->et.gtd;
		dmm.cache->update_eviction_target_translation(dmm.cache, target->lba, dp->et.gtd, NULL,  req->value->value, dp->cache_private, true);
	}else{
		eviction_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
		demand_map_fine_type_pending(req, dp->et.mapping, req->value->value, prefetching_info);
	}

	if(dmm.cache->entry_type==DYNAMIC){
		uint32_t temp_eviction_hint=dmm.eviction_hint-dp->now_eviction_hint;
		if(!dp->is_hit_eviction && dmm.cache->need_more_eviction(dmm.cache, target->lba, prefetching_info, temp_eviction_hint)){
			dmm.eviction_hint=dmm.cache->update_eviction_hint(dmm.cache, target->lba,  prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, false);
			dp->now_eviction_hint=0;
			if((int)dmm.eviction_hint<0){
				printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
				abort();
			}
			dp_status_update(dp, NONE);
			dp_prev_init(dp);
		}
		else{
			dp_status_update(dp, EVICTIONW);
		}
	}
	else{
		dp_status_update(dp, EVICTIONW);
	}
	write_updated_map(req, eviction_etr, dp);
	return 1;
}

void debug_size(){
	uint32_t calc_hint=all_eviction_hint();
	if(dmm.eviction_hint!=calc_hint){
		print_all_processed_req();
		printf("calc_hint: %u\n", calc_hint);
		abort();
	}
}

uint32_t cache_traverse_state(request *req, mapping_entry *now_pair, demand_param *dp, 
		uint32_t *prefetching_info, bool iswrite_path){
	GTD_entry *now_etr=&dmm.GTD[GETGTDIDX(now_pair->lba)];
	GTD_entry *eviction_etr=NULL;
	uint32_t res;
	bool double_eviction=false;
	bool dynamic_flying_hit=false;
	bool all_entry_evicting=false;
	static bool print_all=false;
	static bool debug_flag=false;
	uint32_t temp_res_value;

	if(test_key==now_pair->lba){
		static int cnt=0;
		printf("%d L:%u P:%u\n", cnt++, now_pair->lba, now_pair->ppa);
	}

	if(print_all){
		print_all_processed_req();
	}

retry:
	switch(dp->status){
		case EVICTIONW:
			if(dp->is_hit_eviction){
				return DONE_END;
			}
			if(now_etr->status==EMPTY || dmm.cache->exist(dmm.cache, now_pair->lba)){
				/*direct insert and already exist*/
				dmm.eviction_hint=dmm.cache->update_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, false);
				dp->now_eviction_hint=0;
				if((int)dmm.eviction_hint<0){
					printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
					abort();
				}
				
				if(dmm.cache->entry_type==DYNAMIC){
					goto hit_eviction;
				}
				else{
					dp_status_update(dp, HIT); goto retry;
				}
			}
			else{
			}

			if(dmm.cache->entry_type==DYNAMIC){
				uint32_t temp_size=dmm.eviction_hint-dp->now_eviction_hint;
				if(dmm.cache->get_remain_space(dmm.cache, temp_size) < dp->now_eviction_hint){
					dmm.cache->get_remain_space(dmm.cache, temp_size);
					//print_all_processed_req();					
				//	debug_size();
					//abort();
					dp_status_update(dp, NONE);
					dp_prev_init(dp);
					double_eviction=true;
					DMI.additional_eviction_cnt++;
					goto eviction_path;
				}		
			}

			dp_status_update(dp, MISSR); 
			res=map_read_wrapper(now_etr, req, dmm.li, dp, now_pair->lba, AFTER_EVICTION);
			if(res==FLYING_HIT_END){
				if(dmm.cache->type==COARSE){
					dmm.eviction_hint=dmm.cache->update_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, false);
					dp->now_eviction_hint=0;
					if((int)dmm.eviction_hint<0){
						printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
						abort();
					}

					if(dmm.cache->entry_type==DYNAMIC){
						if(iswrite_path){
							dmm.eviction_hint=dmm.cache->update_hit_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, true);
						}
					}

					if((int)dmm.eviction_hint<0){
						printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
						abort();
					}
				}
			}
			return res;
		case NONE:
			if(dmm.cache->exist(dmm.cache, now_pair->lba)==false){ //MISS
				req->type_ftl|=1;
				/*notfound check!*/
				if(req->type==FS_GET_T && now_etr->physical_address==UINT32_MAX){
					return NOTFOUND_END;
				}
				dmm.cache->update_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, true);
				if((int)dmm.eviction_hint<0){
					printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
					abort();
				}

				if(dmm.cache->entry_type==DYNAMIC && dmm.cache->type==COARSE){/*sftl*/
					if(now_etr->status==FLYING){
						if(iswrite_path){
							dmm.cache->update_hit_eviction_hint(dmm.cache, now_pair->lba, prefetching_info,
									dmm.eviction_hint, &dp->now_eviction_hint, true);
							dynamic_flying_hit=true;
							dmm.eviction_hint+=dp->now_eviction_hint;
						}
						else{
							if(flying_request_hit_check(req,now_pair->lba, dp, true)){
								return FLYING_HIT_END;	
							}
						}
					}
				}
				else{
					if(flying_request_hit_check(req, now_pair->lba, dp, true)) return FLYING_HIT_END;
				}
				
				DMI.miss_num++;
				iswrite_path ? DMI.write_miss_num++ : DMI.read_miss_num++;

				if((temp_res_value=dmm.cache->is_needed_eviction(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint))){
					if(temp_res_value==EMPTY_EVICTION){
						//print_all_processed_req();
						//printf("req->global_seq:%u is retry!!\n", req->global_seq);
						if(dynamic_flying_hit){
							dmm.eviction_hint=dmm.cache->update_hit_eviction_hint(dmm.cache, now_pair->lba, prefetching_info,
									dmm.eviction_hint, &dp->now_eviction_hint, false);					
						}
						dp->now_eviction_hint=0;

						//debug_size();
						dp_status_update(dp, NONE);
						dp_prev_init(dp);

						dp_monitor_update(dp, iswrite_path, M_DELAY);
						if(!inf_assign_try(req)) abort();
						debug_flag=true;
						return RETRY_END;
	//					abort();
					}
					if(dynamic_flying_hit){ 
						//it is flysing hit, but no space for this entry, it may need larger space
						dmm.eviction_hint=dmm.cache->update_hit_eviction_hint(dmm.cache, now_pair->lba, prefetching_info,
								dmm.eviction_hint, &dp->now_eviction_hint, false);
						dmm.cache->update_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, true);
					}

					if(!dp->is_hit_eviction && dmm.cache->is_eviction_hint_full(dmm.cache, dmm.eviction_hint)){
						dp_monitor_update(dp, iswrite_path, M_DELAY);
						if(!inf_assign_try(req)) abort();
						return RETRY_END;
					}
eviction_path:
					
					if(!dp->is_hit_eviction && !double_eviction){
						if(!dp->is_fresh_miss){
							dmm.eviction_hint+=dp->now_eviction_hint;
						}
					}
//eviction_again:
					DMI.eviction_cnt++;

					if(dmm.cache->type==COARSE){
						if(!(dp->et.gtd=dmm.cache->get_eviction_GTD_entry(dmm.cache, now_pair->lba))){
							DMI.clean_eviction++;
							dp_status_update(dp, EVICTIONW); 
							goto retry;
						}
						else{
							eviction_etr=dp->et.gtd;
							DMI.dirty_eviction++;
							updating_mapping_for_eviction(req, dp, now_pair, prefetching_info);
							return MAP_WRITE_END;
						}
					}
					else{
						dp_status_update(dp, EVICTIONR);
						if(!(dp->et.mapping=dmm.cache->get_eviction_mapping_entry(dmm.cache, now_pair->lba, dp->now_eviction_hint, &dp->cache_private, &all_entry_evicting))){
							if(all_entry_evicting){
								dmm.eviction_hint=dmm.cache->update_hit_eviction_hint(dmm.cache, now_pair->lba, prefetching_info,
										dmm.eviction_hint, &dp->now_eviction_hint, false);
								print_all_processed_req();
								return NO_EVICTIONING_ENTRY;
							}
							DMI.clean_eviction++;
							dp_status_update(dp, EVICTIONW); 
							goto retry;	
						}
						/*fine grained dirty eviction*/
						DMI.dirty_eviction++;
						eviction_etr=&dmm.GTD[GETGTDIDX(dp->et.mapping->lba)];
						if(eviction_etr->physical_address==UINT32_MAX){
							fdriver_lock(&eviction_etr->lock);
							list_insert(eviction_etr->pending_req, (void*)req);
							fdriver_unlock(&eviction_etr->lock);
							updating_mapping_for_eviction(req, dp, now_pair, prefetching_info);
							return MAP_WRITE_END;
						}
						else{
							return map_read_wrapper(eviction_etr, req, dmm.li, dp, dp->et.mapping->lba, EVICTION_READ);
						}
					}
				}//needed eviction end
				else{
					req->type==FS_SET_T?DMI.write_cold_miss_num++:DMI.read_cold_miss_num++;
					
					if(dynamic_flying_hit){//has enough space for this entry
						if(iswrite_path){
							/*already update dmm.eviction_hint*/
							//dmm.eviction_hint+=dp->now_eviction_hint;
						}
						else{
							dmm.eviction_hint=dmm.cache->update_hit_eviction_hint(dmm.cache, now_pair->lba, prefetching_info,
									dmm.eviction_hint, &dp->now_eviction_hint, false);
							dp->now_eviction_hint=0;
						}
						fdriver_lock(&now_etr->lock);
						list_insert(now_etr->pending_req, (void*)req);
						fdriver_unlock(&now_etr->lock);
						dp_status_update(dp, MISSR);
						return FLYING_HIT_END;
					}
					else{
						dmm.eviction_hint+=dp->now_eviction_hint;
					}

					if(iswrite_path){
						if(now_etr->physical_address!=UINT32_MAX){
							dp_status_update(dp, MISSR);
							return map_read_wrapper(now_etr, req, dmm.li, dp, now_pair->lba, COLD_MISS_READ);
						}
						else{
							/*
							if(!dp->is_fresh_miss){
								dp->is_fresh_miss=true;
								goto retry;
							}
							dp->now_eviction_hint*=2;
							*/
							dmm.eviction_hint=dmm.cache->update_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, false);
							dp->now_eviction_hint=0;
							dp->is_fresh_miss=false;
							if((int)dmm.eviction_hint<0){
								printf("%s:%d eviction_hint error!\n", __FILE__,__LINE__);
								abort();
							}
							dp_status_update(dp, HIT); goto retry;
						}
					}
					else{
						//read-req should read mapping data
						dp_status_update(dp, MISSR);
						return map_read_wrapper(now_etr, req, dmm.li,dp, req->key, COLD_MISS_READ);
					}
				}
			}//miss end
			else{
hit_eviction:
				if(dmm.cache->entry_type==DYNAMIC &&
						dmm.cache->is_hit_eviction(dmm.cache, now_etr, now_pair->lba, now_pair->ppa, dmm.eviction_hint)){
					if(iswrite_path){
						update_cache_entry_wrapper(now_etr, now_pair->lba, now_pair->ppa, false);
					}
					else{
						now_pair->ppa=get_mapping_wrapper(req, now_pair->lba);
					}
					DMI.hit_eviction++;
					dp->is_hit_eviction=true;
					goto eviction_path;
				}
				dp_status_update(dp, HIT); goto retry;
			}
			abort(); //not covered case 
		case HIT:
			DMI.hit_num++;
			iswrite_path ? DMI.write_hit_num++ : DMI.read_hit_num++;
			if(iswrite_path){
				if(dp->is_fresh_miss){
					dmm.eviction_hint=dmm.cache->update_eviction_hint(dmm.cache, now_pair->lba, prefetching_info, dmm.eviction_hint, &dp->now_eviction_hint, false);
					dp->now_eviction_hint=0;
				}
				update_cache_entry_wrapper(now_etr, now_pair->lba, now_pair->ppa, false);
			}
			else{
				now_pair->ppa=get_mapping_wrapper(req, now_pair->lba);
			}
			return DONE_END;
		case EVICTIONR:
			updating_mapping_for_eviction(req, dp, now_pair, prefetching_info);
			return MAP_WRITE_END;
		case MISSR:
			if(dmm.cache->type==FINE){
				if(demand_map_fine_type_pending(req, now_pair, req->value->value, prefetching_info)){
					return MISS_STATUS_DONE;
				}
			}
			else{
				if(demand_map_coarse_type_pending(req, now_etr, req->value->value)){
					return MISS_STATUS_DONE;
				}
			}
			if(!iswrite_path){
				abort();
			}
			return DONE_END;
		default:
			abort();
	}
}

static inline uint32_t check_flying_req(request *req, assign_param_ex *mp, bool remove){
	return remove?1:L2PGAP;
	uint32_t res=remove?1:L2PGAP;
	std::map<uint32_t, request *>::iterator iter;
	mapping_entry now_entry;
	demand_param *tdp;
	assign_param_ex *tmp;

	for(int32_t i=0; i<res; i++){
		now_entry.lba=mp->lba[i];
		now_entry.ppa=mp->physical[i];
		for(iter=dmm.flying_req->begin(); iter!=dmm.flying_req->end(); iter++){
			request *treq=iter->second;
			tdp=(demand_param *)treq->param;
			tmp=(assign_param_ex*)tdp->param_ex;
			if(tmp->idx==tmp->max_idx) continue;

			for(uint32_t tnow=tmp->idx; tnow<tmp->max_idx; tnow++){
				if(tmp->lba[tnow]==now_entry.lba){
					if(tmp->lba[tnow]==debug_lba){
						printf("%u overlap flyin req, update %u->%u\n", debug_lba, tmp->physical[tnow], now_entry.ppa);				
					}
#ifdef DFTL_DEBUG
					printf("%u %u->%u  is update in flying\n", tmp->lba[tnow], tmp->lba[tnow], now_entry.ppa);
#endif
					invalidate_ppa(tmp->physical[tnow]);
					printf("seq:(%u,%u)tdp:%s lba:%u ppa:%u\n", treq->global_seq, req->global_seq,
							cache_traverse_type(tdp->status), tmp->lba[tnow], tmp->physical[tnow]);
					tmp->physical[tnow]=now_entry.ppa;

					if(i+1<res){
						memmove(&mp->lba[i], &mp->lba[i+1], sizeof(uint32_t) * (res-(i+1)));
						memmove(&mp->physical[i], &mp->physical[i+1], sizeof(uint32_t) * (res-(i+1)));
						memmove(&mp->prefetching_info[i], &mp->prefetching_info[i+1], sizeof(uint32_t) * (res-(i+1)));
					}
					res--;
					i--;
				}
			}
		}
	}
	
	if(res){
		dmm.flying_req->insert(std::pair<uint32_t, request *>(req->seq, req));	
	}
	return res;
}

uint32_t demand_map_assign(request *req, KEYT *_lba, KEYT *_physical, uint32_t *prefetching_info, bool remove){
	uint8_t i=0;
	demand_param *dp;
	assign_param_ex *mp;
	KEYT *lba=NULL, *physical=NULL;
	uint32_t res;
	bool direct_end=false;

	if(!req->param){
		dp=(demand_param*)calloc(1, sizeof(demand_param));
		dp_initialize(dp);

#ifdef CHECKING_TEST_TIME
		measure_init(&dp->mt);
		measure_start(&dp->mt);
#endif
		//lba=_lba; physical=_physical;

		mp=(assign_param_ex*)malloc(sizeof(assign_param_ex));
		if(remove){
			mp->lba=(uint32_t*)malloc(sizeof(KEYT)*L2PGAP);
			mp->physical=(uint32_t*)malloc(sizeof(KEYT)*L2PGAP);
			mp->lba[0]=*_lba;
			mp->physical[0]=UINT32_MAX;
			mp->prefetching_info=(uint32_t*)malloc(sizeof(uint32_t)*L2PGAP);
			mp->prefetching_info[0]=0;
		}
		else{
			cpy_keys(&mp->lba,_lba);
			cpy_keys(&mp->physical, _physical);
			mp->prefetching_info=(uint32_t*)malloc(sizeof(uint32_t)*L2PGAP);
			memcpy(mp->prefetching_info, prefetching_info, sizeof(uint32_t)*L2PGAP);
		}
		i=mp->idx=0;
		dp->param_ex=(void*)mp;
		dp->is_hit_eviction=false;
		dp->flying_map_read_key=UINT32_MAX;
		req->param=(void*)dp;
		mp->max_idx=check_flying_req(req, mp, remove);
		if(mp->max_idx==0){
			direct_end=true;
			goto end;
		}

		lba=mp->lba;
		physical=mp->physical;
		req->round_cnt=0;

		//printf("[in] req->global_seq:%u\n", req->global_seq);
		dmm.all_now_req->insert(std::pair<uint32_t, request*>(req->global_seq, req));
		//send_user_req(req, DATAW, dp->ppa, dp->value);
	}
	else{
		dp=(demand_param*)req->param;
		mp=(assign_param_ex*)dp->param_ex;

		i=mp->idx;
		lba=mp->lba;
		physical=mp->physical;
	}

	for(;i<mp->max_idx; i++){
		mp->idx=i;
		mapping_entry *target=&dp->target;
		target->lba=lba[i];
		target->ppa=physical[i];

		if(target->lba==0 && target->ppa==328916){
			//GDB_MAKE_BREAKPOINT;
		}

		fdriver_lock(&dmi_lock);
		if(!bitmap_is_set(DMI.write_working_set, target->lba)){
			DMI.write_working_set_num++;
			bitmap_set(DMI.write_working_set, target->lba);
		}
		fdriver_unlock(&dmi_lock);

		pthread_mutex_lock(&traverse_lock);
		res=cache_traverse_state(req, target, dp, &mp->prefetching_info[i], true);
		pthread_mutex_unlock(&traverse_lock);

		switch(res){
			case RETRY_END:
				dp_status_update(dp, NONE);
				dp_prev_init(dp);
			case MAP_READ_ISSUE_END:
			case FLYING_HIT_END:
			case MAP_WRITE_END:
			case MISS_STATUS_DONE:
				return res;
			case DONE_END:
				dp_initialize(dp);
				demand_req_data_collect(dp, req->type==FS_SET_T, i+1==mp->max_idx);
				//nxt round
				break;
			case NO_EVICTIONING_ENTRY:
				dp_status_update(dp, NONE);
				dp_prev_init(dp);
				dmm.stopped_request->push_back(req);
				return 0;
				break;
			case NOTFOUND_END:
				printf("not found end in write!\n");
			default:
				abort();
		}
	}

	if(i!=mp->max_idx){
		return 0;
	}
end:
	if(!direct_end){
		dmm.flying_req->erase(req->seq);
	}
	if(i==mp->max_idx){ //done 
		//printf("[de] req->global_seq:%u\n", req->global_seq);
		dmm.all_now_req->erase(req->global_seq);
//		debug_size()
		if(req->param){
			free(mp->lba);
			free(mp->physical);
			free(mp->prefetching_info);
			free(mp);

			free(req->param);
		}
		
		write_done_check(req);
		reinsert_stopped_request();
		return 1;
	}
	return 0;
}

static inline uint32_t check_read_flying_req(request *req){
	std::map<uint32_t, request *>::iterator iter;
	demand_param *tdp;
	assign_param_ex *tmp;

	uint32_t recent_global_seq=0;
	uint32_t res=UINT32_MAX;
	for(iter=dmm.flying_req->begin(); iter!=dmm.flying_req->end(); iter++){	
		request *treq=iter->second;
		if(treq->global_seq > req->global_seq || treq->type!=FS_SET_T) continue;
		tdp=(demand_param *)treq->param;
		tmp=(assign_param_ex*)tdp->param_ex;
		for(uint32_t tnow=tmp->idx; tnow<tmp->max_idx; tnow++){
			if(tmp->lba[tnow]==req->key){
				if(recent_global_seq < treq->global_seq){
					recent_global_seq=treq->global_seq;
					res=tmp->physical[tnow];
				}
			}
		}
	}

	return res;
}

uint32_t demand_page_read(request *const req){
	demand_param *dp;
	assign_param_ex *mp;
	uint32_t res;

	if(!req->param){
		dp=(demand_param*)calloc(1, sizeof(demand_param));
		dp_status_update(dp, NONE);
		dp_prev_init(dp);
		dp->is_hit_eviction=false;
#ifdef CHECKING_TEST_TIME
		measure_init(&dp->mt);
		measure_start(&dp->mt);
#endif
		mp=(assign_param_ex*)malloc(sizeof(assign_param_ex));
		mp->prefetching_info=(uint32_t*)malloc(sizeof(uint32_t));
		mp->idx=0;
		if(req->consecutive_length){
			req->consecutive_length++;
		}

		req->round_cnt=0;
		mp->prefetching_info[0]=req->consecutive_length;
		dp->param_ex=mp;
		dp->target.lba=req->key;
		dp->target.ppa=UINT32_MAX;
		dp->flying_map_read_key=UINT32_MAX;
		req->param=(void*)dp;
		uint32_t recent_ppa;
		if((recent_ppa=check_read_flying_req(req))!=UINT32_MAX){
			dp->target.ppa=recent_ppa;
			goto read_data;
		}

		dmm.all_now_req->insert(std::pair<uint32_t, request*>(req->global_seq, req));
	}else{
		dp=(demand_param*)req->param;
		mp=(assign_param_ex*)dp->param_ex;
	}

	//static uint32_t prev_lba=1437024

	fdriver_lock(&dmi_lock);
	if(!bitmap_is_set(DMI.read_working_set, dp->target.lba)){
		DMI.read_working_set_num++;
		bitmap_set(DMI.read_working_set, dp->target.lba);
	}
	fdriver_unlock(&dmi_lock);

	pthread_mutex_lock(&traverse_lock);
	res=cache_traverse_state(req, &dp->target, dp, &mp->prefetching_info[0], false);
	pthread_mutex_unlock(&traverse_lock);

	switch(res){
		case RETRY_END:
			dp_status_update(dp, NONE);
			dp_prev_init(dp);
		case MAP_READ_ISSUE_END:
		case FLYING_HIT_END:
		case MAP_WRITE_END:
		case MISS_STATUS_DONE:
			return res;
		case DONE_END:
			goto read_data;
		case NO_EVICTIONING_ENTRY:
			dmm.stopped_request->push_back(req);
			return 0;
			break;
		case NOTFOUND_END:	
			dmm.all_now_req->erase(req->global_seq);
			//debug_size();
			notfound_processing(req);
			return 1;
		default:
			abort();
	}

	
read_data:
	__demand_map_pending_read(req, dp, false);
	//done
	reinsert_stopped_request();
	return 1;
}

int mapping_entry_compare(const void * a, const void *b){
	return (int)((mapping_entry*)a)->lba - (int)((mapping_entry*)b)->lba;
}

extern int gc_cnt;
extern uint32_t debug_gc_lba;
uint32_t update_flying_req_data(uint32_t lba, uint32_t ppa){
	std::map<uint32_t, bool>::iterator flag_iter;
	std::map<uint32_t, request*>::iterator req_iter;
retry:
	fdriver_lock(&dmm.flying_map_read_lock);
	flag_iter=dmm.flying_map_read_flag_set->find(GETGTDIDX(lba));
	if(flag_iter!=dmm.flying_map_read_flag_set->end() && 
			flag_iter->first==GETGTDIDX(lba)){
		if(flag_iter->second==false){
			fdriver_unlock(&dmm.flying_map_read_lock);
			goto retry;
		}
		else{
			req_iter=dmm.flying_map_read_req_set->find(flag_iter->first);
			dmm.cache->update_from_translation_gc(dmm.cache, req_iter->second->value->value, lba, ppa);
			//printf("updated ppa:%u %u\n", ppa, ppa/L2PGAP/_PPS);
		}
	}
	fdriver_unlock(&dmm.flying_map_read_lock);
	return 1;
}

uint32_t demand_map_some_update(mapping_entry *target, uint32_t idx){ //for gc
	qsort(target, idx, sizeof(mapping_entry), mapping_entry_compare);
	bool read_start=false;
	uint32_t gtd_idx=0, temp_gtd_idx;
	gc_map_value *gmv;
	list *temp_list=list_init();
	uint32_t old_ppa;

	bool debug_test=false;
	uint32_t debug_gtd_idx;
	uint32_t debug_idx=UINT32_MAX;
	for(uint32_t i=0; i<idx; i++){
		if(target[i].lba==debug_lba){
			printf("gc debug_lba is updated from %u\n", target[i].ppa);
		}
		temp_gtd_idx=GETGTDIDX(target[i].lba);
		update_flying_req_data(target[i].lba, target[i].ppa);

		if(dmm.cache->exist(dmm.cache, target[i].lba) && target[i].ppa!=UINT32_MAX){
			old_ppa=dmm.cache->update_entry_gc(dmm.cache, &dmm.GTD[temp_gtd_idx], target[i].lba, target[i].ppa);
			if(dmm.cache->type==COARSE) continue;
		}
		else{
			if(dmm.GTD[temp_gtd_idx].physical_address==UINT32_MAX) continue;
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
	struct timeval start, end;
	uint64_t time1=0, time2=0;
	while(temp_list->size){
		for_each_list_node_safe(temp_list, now, nxt){
			gmv=(gc_map_value*)now->data;
			if(!gmv->isdone) continue;

			gtd_idx=gmv->gtd_idx;

			for(uint32_t i=gmv->start_idx;  i<idx && GETGTDIDX(target[i].lba)==gtd_idx; i++){
				dmm.cache->update_from_translation_gc(dmm.cache, gmv->value->value, target[i].lba, target[i].ppa);	
			}

			if(dmm.cache->entry_type==DYNAMIC){
				dmm.cache->update_dynamic_size(dmm.cache, gtd_idx*PAGESIZE/sizeof(uint32_t), gmv->value->value);
			}

			invalidate_map_ppa(dmm.GTD[gtd_idx].physical_address);
			uint32_t new_ppa=get_map_ppa(gtd_idx, NULL);
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

static void print_help(){
	printf("-----help------\n");
	printf("parameters (c, p, t)\n");
	printf("-c: set cache size as absolute input value\n");
	printf("\tex: 1M, 1K, 1G\n");
	printf("\tIt cannot over the memory usage for Page FTL\n");
	printf("-p: set cache size as percentage of Page FTL\n");
	printf("\tex: 10 for 10%% of total amount of Page FTL\n");
	printf("-t: cache type \n");
	for(int i=0; i<CACHE_TYPE_MAX_NUM; i++){
		printf("\t%d: %s\n", i, cache_type((cache_algo_type)i));
	}
}
extern uint32_t map_seg_limit;
extern bool limit_map_seg;
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
	limit_map_seg=false;
	while((c=getopt(argc,argv,"ctphl"))!=-1){
		switch(c){
			case 'h':
				print_help();
				exit(1);
				break;
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
				physical_page_num=(SHOWINGSIZE/_K) * cache_percentage;
				physical_page_num/=PAGESIZE;
				break;
			case 'l':
				map_seg_limit=atoi(argv[optind]);
				limit_map_seg=true;
				break;
			default:
				print_help();
				printf("not invalid parameter!!\n");
				abort();
				break;
		}
	}
	
	//cache_type_set=true;
	
	if(cache_type_set){
		switch(c_type){
			case DEMAND_COARSE:
				dmm.cache=&coarse_cache_func;
				dmm.c_type=DEMAND_COARSE;
				break;
			case DEMAND_FINE:
				dmm.cache=&fine_cache_func;
				dmm.c_type=DEMAND_FINE;
				break;
			case SFTL:
				dmm.cache=&sftl_cache_func;
				dmm.c_type=SFTL;
				break;
			case TPFTL:
				dmm.cache=&tp_cache_func;
				dmm.c_type=TPFTL;
				break;
			case NOCPU_SFTL:
				dmm.cache=&nocpu_sftl_cache_func;
				dmm.c_type=NOCPU_SFTL;	
				break;	
		}
	}
	else{
		dmm.c_type=TPFTL;
		dmm.cache=&tp_cache_func;
	}

	if(!cache_size){
		//physical_page_num=((SHOWINGSIZE/K)/4)/PAGESIZE;
		physical_page_num=(((SHOWINGSIZE/_K))/100*21)/PAGESIZE;
	}
	dmm.max_caching_pages=physical_page_num;
	return 1;
}
