#include <list>
#include "./run.h"
#include "./summary_page_set.h"
#include "../../include/sem_lock.h"
#include "./piece_ppa.h"
extern sc_master* shortcut;
extern lower_info *g_li;
bool debug_flag=false;
uint32_t target_recency=9;

typedef struct merge_meta_container{
	run *r;
	sp_set_iter *ssi;
	uint32_t now_proc_block_idx;
	bool done;
}mm_container;

typedef struct __sorted_pair{
	run *r;
	summary_pair pair;
	uint32_t original_psa;
	char *data;
	fdriver_lock_t lock;
	value_set *value;
}__sorted_pair;

static inline uint32_t __set_read_flag(mm_container *mm_set, uint32_t run_num, uint32_t round){
	uint32_t res=0;
	for(uint32_t i=0; i<run_num; i++){
		if(mm_set[i].done || mm_set[i].now_proc_block_idx==round){
			res|=(1<<i);
		}
	}
	return res;
} 

static inline void __invalidate_target(run *r, uint32_t intra_offset){
	uint32_t original_psa=run_translate_intra_offset(r, intra_offset);
	if(original_psa==UNLINKED_PSA) return;
	if(invalidate_piece_ppa(r->st_body->bm->segment_manager, original_psa, true)==BIT_ERROR){
		EPRINT("BIT ERROR piece_ppa: %u", true, original_psa);
	}
}

static inline bool __move_iter_target(mm_container *mm_set, uint32_t idx){
	if(mm_set[idx].done) return true;
	bool res=sp_set_iter_move(mm_set[idx].ssi);
	if(res){
		mm_set[idx].now_proc_block_idx++;
		if(sp_set_iter_done_check(mm_set[idx].ssi)){
			mm_set[idx].done=true;
		}
	}
	return res;
}

static inline uint32_t __move_iter(mm_container *mm_set, uint32_t run_num, uint32_t lba,
		uint32_t read_flag, uint32_t ridx){
	summary_pair res;
	for(uint32_t i=0; i<run_num; i++){
		if(mm_set[i].done) continue;
		res=sp_set_iter_pick(mm_set[i].ssi);
		if(res.lba==UINT32_MAX){
			GDB_MAKE_BREAKPOINT;
		}
		if(res.lba==UINT32_MAX && __move_iter_target(mm_set, i)){
			read_flag|=(1<<i);
			continue;
		}
		if(res.lba==lba){
			if(__move_iter_target(mm_set, i)){
				read_flag|=(1<<i);
			}
	
			if(i!=ridx){
				__invalidate_target(mm_set[i].r, res.intra_offset);
			}
		}
	}
	return read_flag;
}

static inline summary_pair __pick_smallest_pair(mm_container *mm_set, uint32_t run_num, uint32_t *ridx){
	summary_pair res={UINT32_MAX, UINT32_MAX};
	summary_pair now;
	uint32_t t_idx;

retry:
	for(uint32_t i=0; i<run_num; i++){
		if(mm_set[i].done) continue;
		if(i==0 || res.lba==UINT32_MAX){
			*ridx=i;
			res=sp_set_iter_pick(mm_set[i].ssi);
			continue;
		}

		//DEBUG_CNT_PRINT(temp, 371522, __FUNCTION__, __LINE__);
		now=sp_set_iter_pick(mm_set[i].ssi);
		if(res.lba > now.lba){
			res=now;
			*ridx=i;
		}
	}

	t_idx=*ridx;

	if(res.lba==UINT32_MAX){
		__move_iter_target(mm_set, t_idx);
	}
	/*check validataion whether old or not*/
	else if(!shortcut_validity_check_lba(shortcut, mm_set[t_idx].r, res.lba)){
		__invalidate_target(mm_set[t_idx].r, res.intra_offset);
		__move_iter_target(mm_set, t_idx);
		res.lba=UINT32_MAX;
		res.intra_offset=UINT32_MAX;
		goto retry;
	}
	return res;
}

static inline void *__merge_end_req(algo_req* req){
	if(req->type!=COMPACTIONDATAR){
		EPRINT("error type!", true);
	}

	__sorted_pair *t_pair=(__sorted_pair*)req->param;
	fdriver_unlock(&t_pair->lock);
	free(req);
	return NULL;
}

static inline void __merge_issue_req(__sorted_pair *sort_pair){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->type=COMPACTIONDATAR;
	sort_pair->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	fdriver_mutex_init(&sort_pair->lock);
	fdriver_lock(&sort_pair->lock);
	sort_pair->data=&sort_pair->value->value[LPAGESIZE*(sort_pair->original_psa%L2PGAP)];

	req->value=sort_pair->value;
	req->end_req=__merge_end_req;
	req->param=(void*)sort_pair;
	g_li->read(sort_pair->original_psa/L2PGAP, PAGESIZE, req->value, req);
}

static inline void __read_merged_data(run *r, std::list<__sorted_pair> *sorted_list, blockmanager *sm){
	if(r->type==RUN_PINNING){ return;}
	std::list<__sorted_pair>::iterator iter=sorted_list->begin();
	for(; iter!=sorted_list->end(); iter++){
		__sorted_pair *t_pair=&(*iter);
		t_pair->original_psa=run_translate_intra_offset(t_pair->r, t_pair->pair.intra_offset);
		__merge_issue_req(t_pair);
	}
}

static inline void __write_merged_data(run *r, std::list<__sorted_pair> *sorted_list){
	std::list<__sorted_pair>::iterator iter=sorted_list->begin();
	if(r->type==RUN_NORMAL){
		for(; iter!=sorted_list->end();iter++){
			__sorted_pair *t_pair=&(*iter);
			fdriver_lock(&t_pair->lock);
			fdriver_destroy(&t_pair->lock);
			__invalidate_target(t_pair->r, t_pair->pair.intra_offset);
		}
	}

	iter=sorted_list->begin();
	for(; iter!=sorted_list->end(); ){
		__sorted_pair *t_pair=&(*iter);

		if(r->type==RUN_PINNING){
			t_pair->original_psa=run_translate_intra_offset(t_pair->r, t_pair->pair.intra_offset);
			run_insert(r, t_pair->pair.lba, t_pair->original_psa, NULL, true);
		}
		else{
			run_insert(r, t_pair->pair.lba, UINT32_MAX, t_pair->data, true);
			inf_free_valueset(t_pair->value, FS_MALLOC_R);
		}
		sorted_list->erase(iter++);
	}
}

run *run_merge(uint32_t run_num, run **rset, uint32_t map_type, float fpr, L2P_bm *bm, uint32_t run_type){
	DEBUG_CNT_PRINT(run_cnt, UINT32_MAX, __FUNCTION__ , __LINE__);
	uint32_t prefetch_num=CEIL(DEV_QDEPTH, run_num);
	mm_container *mm_set=(mm_container*)malloc(run_num *sizeof(mm_container));
	uint32_t now_entry_num=0;
	for(uint32_t i=0; i<run_num; i++){
		now_entry_num+=rset[i]->now_entry_num;

		mm_set[i].r=rset[i];
		if(rset[i]->mf->type==TREE_MAP){
			mm_set[i].ssi=sp_set_iter_init_mf(rset[i]->mf);
		}
		else{
			mm_set[i].ssi=sp_set_iter_init(rset[i]->st_body->now_STE_num, rset[i]->st_body->sp_meta,
					prefetch_num);
		}
		mm_set[i].now_proc_block_idx=0;
		mm_set[i].done=false;
	}

	run *res=run_factory(map_type, now_entry_num, fpr, bm, run_type);
	shortcut_add_run_merge(shortcut, res, rset, run_num);
	
	uint32_t target_round=0, read_flag;
	std::list<__sorted_pair> sorted_arr;
	__sorted_pair target_sorted_pair;
	uint32_t prev_lba=0;
	while(1){
		target_round++;
		//uint32_t limited_lba=UINT32_MAX;
		//sort meta
		do{
			read_flag=__set_read_flag(mm_set, run_num, target_round);
			if(read_flag==((1<<run_num)-1)){
				break;
			}
			uint32_t ridx;
			summary_pair target=__pick_smallest_pair(mm_set, run_num, &ridx);
			target_sorted_pair.pair=target;
			target_sorted_pair.r=rset[ridx];

			if(target.lba!=UINT32_MAX){
				sorted_arr.push_back(target_sorted_pair);
				/*checking sorting data*/
				if(target.lba!=0 && prev_lba>=target.lba){
					EPRINT("sorting error! prev_lba:%u, target.lba:%u", true, prev_lba, target.lba);
				}
				prev_lba=target.lba;
			}
			read_flag=__move_iter(mm_set, run_num, target.lba, read_flag, ridx);
		}while(read_flag!=((1<<run_num)-1));
		
		//read data
		__read_merged_data(res, &sorted_arr, bm->segment_manager);
		//write data
		__write_merged_data(res, &sorted_arr);

		bool done_flag=true;
		for(uint32_t i=0; i<run_num; i++){
			if(!mm_set[i].done){
				done_flag=false; break;
			}
		}
		if(done_flag) break;
	}

	while(sorted_arr.size()){
		//read data
		__read_merged_data(res, &sorted_arr, bm->segment_manager);
		//write data
		__write_merged_data(res, &sorted_arr);
	}

	for(uint32_t i=0; i<run_num; i++){
		sp_set_iter_free(mm_set[i].ssi);
	}
	free(mm_set);

	run_insert_done(res, true);
	printf("result\n");
	run_print(res,false);
	return res;
}
