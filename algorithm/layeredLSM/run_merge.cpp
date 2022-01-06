#include <list>
#include "./run.h"
#include "./summary_page_set.h"
#include "../../include/sem_lock.h"
#include "./piece_ppa.h"
#include "./lsmtree.h"
#include "./merge_helper.h"
#include "./gc.h"
extern lower_info *g_li;
bool debug_flag=false;
extern uint32_t target_PBA;
extern uint32_t test_key;
typedef struct merge_meta_container{
	run *r;
	sp_set_iter *ssi;
	uint32_t now_proc_block_idx;
	bool done;
}mm_container;

static inline uint32_t __set_read_flag(mm_container *mm_set, uint32_t run_num, uint32_t round){
	uint32_t res=0;
	for(uint32_t i=0; i<run_num; i++){
		if(mm_set[i].done || mm_set[i].now_proc_block_idx>=round){
			res|=(1<<i);
		}
	}
	return res;
} 

static inline void __invalidate_target(run *r, uint32_t ste, uint32_t intra_offset){
	uint32_t original_psa=run_translate_intra_offset(r, ste, intra_offset);
	if(original_psa==UNLINKED_PSA) return;
	if(invalidate_piece_ppa(r->st_body->bm->segment_manager, original_psa, true)==BIT_ERROR){
		EPRINT("BIT ERROR piece_ppa: %u", true, original_psa);
	}
}

static inline bool __move_iter_target(mm_container *mm_set, uint32_t idx){
	if(mm_set[idx].done) return true;
	uint32_t prev_now=mm_set[idx].now_proc_block_idx;
	mm_set[idx].now_proc_block_idx=sp_set_iter_move(mm_set[idx].ssi);
	if (sp_set_iter_done_check(mm_set[idx].ssi)){
			mm_set[idx].done = true;
	}
	return mm_set[idx].now_proc_block_idx!=prev_now;
}

static inline uint32_t __move_iter(mm_container *mm_set, uint32_t run_num, uint32_t lba,
		uint32_t read_flag, uint32_t ridx, uint32_t target_round){
	summary_pair res;
	for(uint32_t i=0; i<run_num; i++){
		if(mm_set[i].done) continue;
		res=sp_set_iter_pick(mm_set[i].ssi);
		if(res.lba==UINT32_MAX && __move_iter_target(mm_set, i)){
			if(mm_set[i].now_proc_block_idx>=target_round){
				read_flag |= (1 << i);
			}
			continue;
		}
		if(res.lba==lba){
			if(i!=ridx){
				__invalidate_target(mm_set[i].r, sp_set_get_ste_num(mm_set[i].ssi, res.intra_offset), res.intra_offset);
			}

			if(__move_iter_target(mm_set, i)){
				if (mm_set[i].now_proc_block_idx >= target_round){
					read_flag |= (1 << i);
				}
			}
		}
	}
	return read_flag;
}

static inline summary_pair __pick_smallest_pair(mm_container *mm_set, uint32_t run_num, uint32_t *ridx,
sc_master *shortcut){
	summary_pair res={UINT32_MAX, UINT32_MAX};
	summary_pair now;
	uint32_t t_idx;
	summary_page_meta *spm;

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
	if(res.lba==test_key){
		//EPRINT_CNT(test, 3, "picked", false);
	}
	if(res.lba==UINT32_MAX){
		__move_iter_target(mm_set, t_idx);
	}
	/*check validataion whether old or not*/
	else if(!shortcut_validity_check_lba(shortcut, mm_set[t_idx].r, res.lba)){
		__invalidate_target(mm_set[t_idx].r, sp_set_get_ste_num(mm_set[t_idx].ssi, res.intra_offset), res.intra_offset);
		__move_iter_target(mm_set, t_idx);
		res.lba=UINT32_MAX;
		res.intra_offset=UINT32_MAX;
		goto retry;
	}
	else if((spm=sp_set_check_trivial_old_data(mm_set[t_idx].ssi))!=NULL){
		uint32_t original_level=spm->original_level;
		uint32_t original_recency=spm->original_recency;
		if(!shortcut_validity_check_by_value(shortcut, mm_set[t_idx].r, original_level, original_recency, res.lba)){
			__invalidate_target(mm_set[t_idx].r, sp_set_get_ste_num(mm_set[t_idx].ssi, res.intra_offset), res.intra_offset);
			__move_iter_target(mm_set, t_idx);
			res.lba = UINT32_MAX;
			res.intra_offset = UINT32_MAX;
			goto retry;
		}
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

static inline void __read_merged_data(run *r, std::list<__sorted_pair> *sorted_list,
 blockmanager *sm){
	if(r->type==RUN_PINNING){ return;}
	std::list<__sorted_pair>::iterator iter=sorted_list->begin();
	for(; iter!=sorted_list->end(); iter++){
		__sorted_pair *t_pair=&(*iter);
		t_pair->original_psa=run_translate_intra_offset(t_pair->r,t_pair->ste_num, t_pair->pair.intra_offset);
		if(t_pair->pair.lba==test_key){
			EPRINT("target_read psa:%u", false, t_pair->original_psa);
		}
		__merge_issue_req(t_pair);
	}
}

extern lsmtree *LSM;
static inline void __write_merged_data(run *r, std::list<__sorted_pair> *sorted_list, 
	sc_master *shortcut){
	std::list<__sorted_pair>::iterator iter=sorted_list->begin();
	if(r->type==RUN_NORMAL){
		for(; iter!=sorted_list->end();iter++){
			__sorted_pair *t_pair=&(*iter);
			fdriver_lock(&t_pair->lock);
			fdriver_destroy(&t_pair->lock);
			__invalidate_target(t_pair->r, t_pair->ste_num, t_pair->pair.intra_offset);
		}
	}

	if (debug_flag)
	{
		printf("free_block_num:%u\n", gc_check_free_enable_space(LSM->bm));
	}

	iter=sorted_list->begin();
	static uint32_t cnt=0;
	for(; iter!=sorted_list->end(); ){
		__sorted_pair *t_pair=&(*iter);
		if(r->type==RUN_PINNING){
			t_pair->original_psa=run_translate_intra_offset(t_pair->r, t_pair->ste_num, 
			t_pair->pair.intra_offset);
			
			if(!run_insert(r, t_pair->pair.lba, t_pair->original_psa, NULL, true, shortcut)){
				__invalidate_target(t_pair->r, t_pair->ste_num, t_pair->pair.intra_offset);
			}
		}
		else{
			//DEBUG_CNT_PRINT(test, UINT32_MAX, __FUNCTION__, __LINE__);
			if(!run_insert(r, t_pair->pair.lba, UINT32_MAX, t_pair->data, true, shortcut)){

			}
			inf_free_valueset(t_pair->value, FS_MALLOC_R);
		}
		sorted_list->erase(iter++);
	}
}

static inline void __check_disjoint_spm(run **rset, uint32_t run_num, mm_container *mm_set){
	uint32_t *spm_idx=(uint32_t*)calloc(run_num, sizeof(uint32_t));
	bool **disjoint_check=(bool **)malloc(run_num * sizeof(bool*));
	for(uint32_t i=0; i<run_num; i++){
		disjoint_check[i]=(bool *)malloc(rset[i]->st_body->now_STE_num* sizeof(bool));
		memset(disjoint_check[i], true, rset[i]->st_body->now_STE_num *sizeof(bool));
	}

	for(uint32_t i=0; i<run_num; i++){
		run *r=rset[i];
		for(uint32_t j=0; j<r->st_body->now_STE_num; j++){
			if(rset[i]->st_body->pba_array[j].PBA==UINT32_MAX){
				disjoint_check[i][j]=false;
				continue;
			}
			if(!rset[i]->st_body->sp_meta[j].sorted){
				disjoint_check[i][j]=false;
				continue;
			}
			if(rset[i]->st_body->sp_meta[j].unlinked_data_copy){
				disjoint_check[i][j]=false;
				continue;
			}		

			if(disjoint_check[i][j]==false) continue;
			summary_page_meta *temp=&rset[i]->st_body->sp_meta[j];
			bool check_self=rset[i]->type==RUN_LOG?true:false;

			for(uint32_t k=0; k<run_num; k++){
				uint32_t set_idx;
				if(check_self==false && k==i){
					continue;
				}
				if(check_self==false && rset[k]->type==RUN_NORMAL){
					set_idx = spm_joint_check(rset[k]->st_body->sp_meta, rset[k]->st_body->now_STE_num, temp);
					
					/*
					uint32_t set_idx_debug = spm_joint_check_debug(rset[k]->st_body->sp_meta, rset[k]->st_body->now_STE_num, temp);
					if(set_idx!=set_idx_debug){
						EPRINT("joint error",false);
						GDB_MAKE_BREAKPOINT; //not be commented
						set_idx = spm_joint_check(rset[k]->st_body->sp_meta, rset[k]->st_body->now_STE_num, temp);
					}
					*/
				}
				else{
					set_idx = spm_joint_check_debug(rset[k]->st_body->sp_meta, rset[k]->st_body->now_STE_num, temp, (check_self && k==i)?j:UINT32_MAX);
				}
				if(set_idx==UINT32_MAX || (k==i && j==set_idx)){
					continue;
				}
				else{
					disjoint_check[i][j]=false;
					disjoint_check[k][set_idx]=false;
					break;
				}
			}

			if(disjoint_check[i][j]){
				sp_set_iter_skip_lba(mm_set[i].ssi, j, rset[i]->st_body->sp_meta[j].start_lba,
					rset[i]->st_body->sp_meta[j].end_lba);
			}
		}
	}

	for(uint32_t i=0; i<run_num; i++){
		free(disjoint_check[i]);
	}
	free(disjoint_check);
	free(spm_idx);
}

extern uint32_t test_key;
uint32_t trivial_move(run *r, sc_master *shortcut, mm_container *mm, summary_pair now){
	//DEBUG_CNT_PRINT(test, UINT32_MAX, __FUNCTION__, __LINE__);
	run_padding_current_block(r);
	uint32_t i=0;
	map_function *mf=NULL;
	uint32_t target_ste;
	if(mm->ssi->differ_map){
		mf=map_function_factory(r->st_body->param, MAX_SECTOR_IN_BLOCK);
	}
	target_ste = sp_set_get_ste_num(mm->ssi, now.intra_offset);
	uint32_t res= mm->r->st_body->sp_meta[target_ste].end_lba;
	//printf("%u ~ %u\n", mm->r->st_body->sp_meta[target_ste].start_lba, mm->r->st_body->sp_meta[target_ste].end_lba);

	bool unlinked_data_copy=false;
	uint32_t des_ste_num=r->st_body->now_STE_num;
	run_copy_ste_to(r, &mm->r->st_body->pba_array[target_ste], &mm->r->st_body->sp_meta[target_ste], mf, false);
	uint32_t original_level=mm->r->info->level_idx;
	uint32_t original_recency=mm->r->info->recency;
	while(1){
		summary_pair target = sp_set_iter_pick(mm->ssi);
		if(mf){
			mf->insert(mf, target.lba, i++);
		}

		if(target.lba==test_key){
			uint32_t original_psa=run_translate_intra_offset(mm->r, target_ste, target.intra_offset);

			uint32_t target_ste2=st_array_get_target_STE(r->st_body, test_key);
			uint32_t read_psa=run_translate_intra_offset(r, target_ste2, mm->r->type==RUN_LOG?target.intra_offset%512:target.intra_offset);
			EPRINT("trivial target move:%u, %u\n",false, original_psa, read_psa);
			if(original_psa!=read_psa){
				target_ste2=st_array_get_target_STE(r->st_body, test_key);
				read_psa=run_translate_intra_offset(r, target_ste2, target.intra_offset);
			}
		}

		if(!shortcut_validity_check_and_link(shortcut,mm->r, r ,target.lba)){
			unlinked_data_copy=true;
			r->info->unlinked_lba_num++;
		}

	/*
		if(shortcut_validity_check_lba(shortcut, mm->r, target.lba)){
			shortcut_unlink_and_link_lba(shortcut, r, target.lba);
		}
		else{
			unlinked_data_copy=true;
			r->info->unlinked_lba_num++;
		}
*/
		r->now_entry_num++;

		if (target.lba == res){
			if(mf){
				mf->make_done(mf);
			}
			break;
		}
		__move_iter_target(mm, 0);
	}
	run_copy_unlinked_flag_update(r, des_ste_num, unlinked_data_copy, original_level, original_recency);
	//run_copy_ste_to(r, &mm->r->st_body->pba_array[target_ste], &mm->r->st_body->sp_meta[target_ste], mf, false);
	sp_set_iter_move_ste(mm->ssi, target_ste, res);
	if (sp_set_iter_done_check(mm->ssi)){
			mm->done = true;
	}
	return res;
}

static inline void __sorted_array_flush(run *res, std::list<__sorted_pair> *sorted_arr, lsmtree *lsm)
{
	if(debug_flag){
		//GDB_MAKE_BREAKPOINT;
		printf("sorted_array size %u\n", sorted_arr->size());
	}
	//read data
	__read_merged_data(res, sorted_arr, lsm->bm->segment_manager);
	//write data
	__write_merged_data(res, sorted_arr, lsm->shortcut);
}

typedef struct thread_req{
	fdriver_lock_t lock;
	std::list<__sorted_pair> sroted_list;
}thread_req;

void thread_sorting(void* arg, int thread_num){

}

void run_merge(uint32_t run_num, run **rset, run *target_run, bool force, lsmtree *lsm){
	DEBUG_CNT_PRINT(run_cnt, UINT32_MAX, __FUNCTION__ , __LINE__);
	static int cnt=0;
	if(++cnt==270){
		debug_flag=true;
	}
	uint32_t prefetch_num=CEIL(DEV_QDEPTH, run_num);
	mm_container *mm_set=(mm_container*)malloc(run_num *sizeof(mm_container));
	uint32_t now_entry_num=0;

	bool trivial_move_flag=!force;

	for(uint32_t i=0; i<run_num; i++){
		now_entry_num+=rset[i]->now_entry_num;

		shortcut_set_compaction_flag(rset[i]->info, true);
		mm_set[i].r=rset[i];
		if(rset[i]->type==RUN_LOG){
			mm_set[i].ssi=sp_set_iter_init_mf(rset[i]->st_body->now_STE_num, rset[i]->st_body->sp_meta, 
			rset[i]->run_log_mf->now_contents_num, rset[i]->run_log_mf, rset[i]->st_body->param.map_type!=target_run->st_body->param.map_type);
		}
		else{
			mm_set[i].ssi=sp_set_iter_init(rset[i]->st_body->now_STE_num, rset[i]->st_body->sp_meta, prefetch_num,rset[i]->st_body->param.map_type!=target_run->st_body->param.map_type);
		}
		mm_set[i].now_proc_block_idx=0;
		mm_set[i].done=false;
	}

	if(trivial_move_flag){
		__check_disjoint_spm(rset,run_num, mm_set);
	}

	uint32_t target_round=0, read_flag;
	std::list<__sorted_pair> sorted_arr;
	__sorted_pair target_sorted_pair;
	uint32_t prev_lba=UINT32_MAX;

	bool isstart=true;
	run *res=target_run;
	while(1){
		target_round+=force?rset[0]->st_body->now_STE_num:4*BPS/run_num;
		//uint32_t limited_lba=UINT32_MAX;
		//sort meta
		do{
			read_flag=__set_read_flag(mm_set, run_num, target_round);
			if(read_flag==((1<<run_num)-1)){
				break;
			}
			uint32_t ridx;
			summary_pair target=__pick_smallest_pair(mm_set, run_num, &ridx, lsm->shortcut);
			target_sorted_pair.pair=target;
			target_sorted_pair.r=rset[ridx];
			target_sorted_pair.ste_num=sp_set_get_ste_num(mm_set[ridx].ssi, target.intra_offset);
			if(target.lba!=UINT32_MAX){
				uint32_t end_lba;
				if(trivial_move_flag && sp_set_noncopy_check(mm_set[ridx].ssi, target.lba, &end_lba)){
					if(sorted_arr.size()){
						__sorted_array_flush(res, &sorted_arr, lsm);
					}
					prev_lba=trivial_move(res, lsm->shortcut, &mm_set[ridx], target);
					continue;
				}
				sorted_arr.push_back(target_sorted_pair);

				/*checking sorting data*/
				if(!isstart && prev_lba>=target.lba){
					EPRINT("sorting error! prev_lba:%u, target.lba:%u", true, prev_lba, target.lba);
				}
				else if(isstart){
					isstart=false;
				}
				prev_lba=target.lba;
				read_flag=__move_iter(mm_set, run_num, target.lba, read_flag, ridx, target_round);
			}
		}while(read_flag!=((1<<run_num)-1));
		
		if(sorted_arr.size()){
			__sorted_array_flush(res, &sorted_arr, lsm);
		}

		bool done_flag=true;
		for(uint32_t i=0; i<run_num; i++){
			if(!mm_set[i].done){
				done_flag=false; break;
			}
		}
		if(done_flag) break;
	}

	while(sorted_arr.size()){
		__sorted_array_flush(res, &sorted_arr, lsm);
	}

	for(uint32_t i=0; i<run_num; i++){
		shortcut_set_compaction_flag(rset[i]->info, false);
		sp_set_iter_free(mm_set[i].ssi);
	}
	free(mm_set);
	run_insert_done(res, true);
}

void run_recontstruct(lsmtree *lsm, run *src, run *des, bool force){
	run_merge(1, &src, des, force, lsm);
}
