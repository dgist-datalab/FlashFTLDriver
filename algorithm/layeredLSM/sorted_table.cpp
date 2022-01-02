#include "sorted_table.h"
#include "./mapping_function.h"
#include "./piece_ppa.h"
#include "../../include/search_template.h"


extern bool debug_flag;
sa_master *sa_m;

void sorted_array_master_init(uint32_t total_run_num){
	sa_m=(sa_master*)malloc(sizeof(sa_master));
	sa_m->now_sid_info=0;
	sa_m->sid_queue=new std::queue<uint32_t>();
	for(uint32_t i=0; i<total_run_num; i++){
		sa_m->sid_queue->push(i);
	}
	sa_m->sid_map=(sid_info*)calloc(total_run_num, sizeof(sid_info));
	sa_m->total_run_num=total_run_num;
}

void sorted_array_master_free(){
	delete sa_m->sid_queue;
	free(sa_m->sid_map);
	free(sa_m);
}

static sid_info* __find_sid_info_iter(uint32_t sid){
	return &sa_m->sid_map[sid];
}

static uint32_t __get_empty_sid_idx(){
	uint32_t sa_idx=sa_m->sid_queue->front();
	sa_m->sid_queue->pop();
	return sa_idx;
}

static inline void __release_sid_info_idx(uint32_t idx){
	memset(&sa_m->sid_map[idx], 0, sizeof(sid_info));
	sa_m->sid_queue->push(idx);
}

sid_info* sorted_array_master_get_info(uint32_t sid){
	return __find_sid_info_iter(sid);
}

st_array *st_array_init(run *r, uint32_t max_sector_num, L2P_bm *bm, bool pinning, map_param param){
	st_array *res=(st_array*)calloc(1, sizeof(st_array));
	res->bm=bm;
	res->max_STE_num=CEIL(max_sector_num, MAX_SECTOR_IN_BLOCK);
	res->pba_array=(STE*)malloc((res->max_STE_num+1) * (sizeof(STE)));
	memset(res->pba_array, -1, (res->max_STE_num+1)*sizeof(STE));

	sa_m->now_sid_info%=sa_m->total_run_num;
	res->sp_meta=(summary_page_meta*)calloc(res->max_STE_num+1, sizeof(summary_page_meta));
	for(uint32_t i=0; i<res->max_STE_num+1; i++){
		res->sp_meta[i].start_lba=UINT32_MAX;
	}

	res->now_STE_num=0;
	res->type=pinning?ST_PINNING: ST_NORMAL;
	if(res->type==ST_PINNING){
		res->pinning_data=(uint32_t *)malloc(max_sector_num * sizeof(uint32_t));
		res->gced_unlink_data=bitmap_init(max_sector_num);
	}
	else{
		res->pinning_data=NULL;
	}

	res->now_trivial_ste_copy=false;
	res->param=param;

	uint32_t info_idx=__get_empty_sid_idx();
	res->sid=info_idx;
	sid_info* temp=&sa_m->sid_map[info_idx];
	if(temp->sid!=0){
		EPRINT("alread assigned sid_info(idx):%u\n", true, temp->sid);
		EPRINT("alread assigned sid_info at run idx:%u\n", true, temp->r->run_idx);
	}
	temp->sid=res->sid; temp->sa=res; temp->r=r;

	return res;
}

void st_array_free(st_array *sa){
	for(uint32_t i=0; i<sa->now_STE_num; i++){
		if(invalidate_ppa(sa->bm->segment_manager, sa->sp_meta[i].ppa, true)
				==BIT_ERROR){
			EPRINT("invalidate map error!", true);
		}
		map_function *mf=sa->pba_array[i].mf;
		mf->free(mf);
	}

	__release_sid_info_idx(sa->sid);

	if(sa->type==ST_PINNING){
		free(sa->pinning_data);
		bitmap_free(sa->gced_unlink_data);
	}

	free(sa->sp_meta);
	free(sa->pba_array);
	free(sa);
}

void st_array_set_now_PBA(st_array *sa, uint32_t PBA, bool is_trivial_copy){
	sa->pba_array[sa->now_STE_num].PBA=PBA;
	sa->pba_array[sa->now_STE_num].start_write_pointer=sa->global_write_pointer;
	if(is_trivial_copy){
		L2PBm_is_nomral_block(sa->bm, sa->pba_array[sa->now_STE_num].PBA);
		L2PBm_move_owner(sa->bm, sa->pba_array[sa->now_STE_num].PBA, sa->sid, sa->now_STE_num);
	}
	else{
		L2PBm_make_map(sa->bm, sa->pba_array[sa->now_STE_num].PBA, sa->sid, 
				sa->now_STE_num);
	}
	if(sa->pba_array[sa->now_STE_num].mf){
		EPRINT("already assigned map", true);
	}
	sa->pba_array[sa->now_STE_num].mf=map_function_factory(sa->param, MAX_SECTOR_IN_BLOCK);
}

int compare_intra(STE a, uint32_t target){
	if(a.start_write_pointer<= target && target<=a.start_write_pointer+a.max_offset){
		return 0;
	}
	if(a.start_write_pointer>target){
		return 1;
	}
	else{
		return -1;
	}
}

static inline uint32_t get_run_chunk_by_intra_idx(st_array *sa, uint32_t intra_idx){
	if(sa->internal_fragmented==false){
		return intra_idx/MAX_SECTOR_IN_BLOCK;
	}
	uint32_t target_idx;
	bs_search(sa->pba_array, 0, sa->now_STE_num, intra_idx, compare_intra, target_idx);
	return target_idx;
}

uint32_t st_array_read_translation(st_array *sa, uint32_t intra_idx){
	if(sa->type==ST_PINNING){
		if(bitmap_is_set(sa->gced_unlink_data, intra_idx)){
			return UNLINKED_PSA;
		}

		/*
		uint32_t run_chunk_idx=get_run_chunk_by_intra_idx(sa, intra_idx);
		if(sa->pba_array[run_chunk_idx].PBA!=UINT32_MAX){
			uint32_t physical_page_addr = sa->pba_array[run_chunk_idx].PBA;
			return physical_page_addr * L2PGAP + intra_idx-sa->pba_array[run_chunk_idx].start_write_pointer;
		}
		else{*/
			return sa->pinning_data[intra_idx];
		//}
	}
	uint32_t run_chunk_idx=get_run_chunk_by_intra_idx(sa, intra_idx);
	uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;
	return physical_page_addr*L2PGAP+intra_idx-sa->pba_array[run_chunk_idx].start_write_pointer;
}

map_function *st_array_get_target_STE(st_array *sa, uint32_t lba){

}

uint32_t st_array_summary_translation(st_array *sa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}
	return L2PBm_get_map_ppa(sa->bm)*L2PGAP;
}

uint32_t st_array_write_translation(st_array *sa){
	if(sa->summary_write_alert){
		EPRINT("it is write_summary_order", true);
	}

	if(sa->inblock_write_pointer%MAX_SECTOR_IN_BLOCK==0){
		st_array_set_now_PBA(sa, L2PBm_pick_empty_PBA(sa->bm), false);
	}

	uint32_t run_chunk_idx=sa->now_STE_num;
	uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;

	uint32_t res=physical_page_addr*L2PGAP+sa->inblock_write_pointer;
	return res;
}

uint32_t st_array_insert_pair(st_array *sa, uint32_t lba, uint32_t psa){
	if(sa->summary_write_alert){
		EPRINT("it is write_summary_order", true);
	}

	if(sa->now_STE_num >= sa->max_STE_num){
		EPRINT("sorted_table is full!", true);
	}

	if(sa->sp_meta[sa->now_STE_num].pr_type==NO_PR){
		sa->sp_meta[sa->now_STE_num].private_data=(void *)sp_init();
		sa->sp_meta[sa->now_STE_num].pr_type=WRITE_PR;
	}
	else if(sa->sp_meta[sa->now_STE_num].pr_type==READ_PR){
		EPRINT("not allowed", true);
	}

	if(sa->sp_meta[sa->now_STE_num].start_lba > lba){
		sa->sp_meta[sa->now_STE_num].start_lba=lba;
	}
	if(sa->sp_meta[sa->now_STE_num].end_lba < lba){
		sa->sp_meta[sa->now_STE_num].end_lba=lba;
	}

	summary_page *sp=(summary_page*)sa->sp_meta[sa->now_STE_num].private_data;
	sp_insert(sp, lba, sa->global_write_pointer);

	map_function *mf=sa->pba_array[sa->now_STE_num].mf;
	mf->insert(mf, lba, sa->inblock_write_pointer);

	if(sa->type==ST_PINNING){	
		sa->pinning_data[sa->global_write_pointer]=psa;
		/*fragment block check*/
		if(sa->now_trivial_ste_copy==false){
			L2PBm_block_fragment(sa->bm, psa/L2PGAP, sa->sid);
		}
	}

	sa->global_write_pointer++;
	sa->inblock_write_pointer++;
	if(sa->inblock_write_pointer%MAX_SECTOR_IN_BLOCK==0){
		sa->summary_write_alert=true;
	}
	return 0;
}

uint32_t st_array_force_skip_block(st_array *sa){
	if(sa->now_STE_num >= sa->max_STE_num){
		EPRINT("sorted_table is full!", true);
	}
	if(sa->sp_meta[sa->now_STE_num].pr_type==NO_PR){
		return 0;
	}
	else if(sa->sp_meta[sa->now_STE_num].pr_type==READ_PR){
		EPRINT("not allowed", true);
	}

	summary_page *sp=(summary_page*)sa->sp_meta[sa->now_STE_num].private_data;
	sp_insert(sp, UINT32_MAX, NOT_POPULATE_PSA);
	sa->summary_write_alert=true;
	return 1;
}

void st_array_update_pinned_info(st_array *sa, uint32_t intra_offset, uint32_t new_psa, uint32_t old_psa){
	if(sa->type!=ST_PINNING){
		EPRINT("this function only called in pinned SA", true);
	}
	if(sa->pinning_data[intra_offset]!=old_psa){
		EPRINT("different psa inserted", true);
	}
	sa->pinning_data[intra_offset]=new_psa;
	L2PBm_block_fragment(sa->bm, new_psa/L2PGAP, sa->sid);
}

void st_array_unlink_bit_set(st_array *sa, uint32_t intra_offset, uint32_t old_psa){
	if(sa->type!=ST_PINNING){
		EPRINT("this function only called in pinned SA", true);
	}
	if(sa->pinning_data[intra_offset]!=old_psa){
		EPRINT("different psa inserted", true);
	}
	bitmap_set(sa->gced_unlink_data, intra_offset);
	return;
}

summary_write_param* st_array_get_summary_param(st_array *sa, uint32_t ppa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}

	if(sa->sp_meta[sa->now_STE_num].pr_type==NO_PR){
		return NULL;
	}

	sa->sp_meta[sa->now_STE_num].ppa=ppa;
	summary_page *sp=(summary_page*)sa->sp_meta[sa->now_STE_num].private_data;
	sa->summary_write_alert=false;

	summary_write_param *swp=(summary_write_param*)malloc(sizeof(summary_write_param));
	swp->idx=sa->now_STE_num;
	swp->sa=sa;
	swp->spm=&sa->sp_meta[sa->now_STE_num];
	swp->oob[0]=sa->sid;
	swp->oob[1]=sa->now_STE_num;
	st_array_finish_now_PBA(sa);
	swp->value=sp_get_data((summary_page*)(sa->sp_meta[sa->now_STE_num].private_data));
	sa->now_STE_num++;
	return swp;
}

void st_array_summary_write_done(summary_write_param *swp){
	st_array *sa=swp->sa;
	sp_free((summary_page*)sa->sp_meta[swp->idx].private_data);
	sa->sp_meta[swp->idx].pr_type=NO_PR;
	free(swp);
}