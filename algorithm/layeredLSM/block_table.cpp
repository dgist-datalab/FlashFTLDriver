#include "block_table.h"
#include "compaction.h"
#include "./piece_ppa.h"
#include "./gc.h"
#define NO_MAP UINT32_MAX
#define NO_SEG UINT32_MAX

L2P_bm *bm_master;

L2P_bm *L2PBm_init(blockmanager *sm, uint32_t run_num){
	if(run_num > MAX_RUN_NUM_FOR_FRAG){
		EPRINT("max run_num is over the MAX_RUN_NUM_FOR_FRAG!", true);
	}
	L2P_bm *res=(L2P_bm*)calloc(1, sizeof(L2P_bm));
	res->PBA_map=(block_info*)malloc(_NOB * sizeof(block_info));
	memset(res->PBA_map, -1, _NOB*sizeof(block_info));
	for(uint32_t i=0; i<_NOB; i++){
		res->PBA_map[i].type=LSM_BLOCK_EMPTY;
	}

	res->gc_lock=(bool*)calloc(_NOB, sizeof(bool));

	res->now_seg_idx=NO_SEG;
	res->total_seg_num=_NOS;
	res->seg_trimed_block_num=(uint32_t*)calloc(_NOS, sizeof(uint32_t));
	res->seg_block_bit=(bitmap**)calloc(_NOS, sizeof(bitmap*));
	for(uint32_t i=0; i<res->total_seg_num; i++){
		res->seg_block_bit[i]=bitmap_init(BPS);
	}
	res->segment_manager=sm;
	res->seg_type=(uint32_t*)calloc(_NOS, sizeof(uint32_t));

	res->reserve_seg=sm->get_segment(sm, BLOCK_RESERVE);
	L2PBm_set_seg_type(res, res->reserve_seg->seg_idx, DATA_SEG);
	res->reserve_block_idx=0;
	res->reserve_summary_seg=sm->get_segment(sm, BLOCK_RESERVE);
	L2PBm_set_seg_type(res, res->reserve_summary_seg->seg_idx, SUMMARY_SEG);
	res->now_summary_seg=sm->get_segment(sm, BLOCK_ACTIVE);
	L2PBm_set_seg_type(res, res->now_summary_seg->seg_idx, SUMMARY_SEG);
	bm_master=res;
	fdriver_mutex_init(&res->data_block_lock);
	fdriver_mutex_init(&res->map_block_lock);
	return res;
}


void L2PBm_free(L2P_bm *bm){
	free(bm->gc_lock);
	free(bm->PBA_map);
	free(bm->seg_trimed_block_num);
	for(uint32_t i=0; i<_NOS; i++){
		bitmap_free(bm->seg_block_bit[i]);
	}
	free(bm->seg_type);
	free(bm->seg_block_bit);
	free(bm);
}

void L2PBm_invalidate_PBA(L2P_bm *bm, uint32_t PBA){
	uint32_t seg_idx=PBA/_PPS;
	uint32_t intra_offset=PBA%BPS;
	bitmap_set(bm->seg_block_bit[seg_idx], intra_offset);
	bm->seg_trimed_block_num++;

	bm->PBA_map[PBA/_PPB].sid=NO_MAP;

	if(bm->seg_trimed_block_num[seg_idx]==BPS){
		bitmap_reinit(bm->seg_block_bit[seg_idx], BPS);
		bm->seg_trimed_block_num=0;
	}

}

extern lsmtree *LSM;
uint32_t L2PBm_pick_empty_PBA(L2P_bm *bm){
	fdriver_lock(&bm->data_block_lock);
retry:
	if(bm->now_seg_idx==NO_SEG){
		blockmanager *sm=bm->segment_manager;
		if(sm->is_gc_needed(sm)){
			if(gc(bm, DATA_SEG)==GC_COPY){
				if(bm->now_block_idx==BPS){
					lsmtree_run_print(LSM);
					//lsmtree_print_log(LSM);
					EPRINT("gc not effective", true);
					bm->now_seg_idx=NO_SEG;
					//compaction_clean_last_level(LSM);
					goto retry;
				}
				goto out;
			}
		}
		
		__segment *seg=bm->segment_manager->get_segment(bm->segment_manager, BLOCK_ACTIVE);
		L2PBm_set_seg_type(bm, seg->seg_idx, DATA_SEG);
		bm->now_seg_idx=seg->seg_idx;
		bm->now_block_idx=0;
		
	}
out:
	uint32_t res=bm->now_seg_idx*_PPS+(bm->now_block_idx++)*_PPB;
	if(bm->now_block_idx==BPS){
		bm->now_seg_idx=NO_SEG;
	}

	if(res/_PPB==test_piece_ppa/L2PGAP/_PPB){
		printf("%u populate for data\n", test_piece_ppa/L2PGAP/_PPB);
	}	
	fdriver_unlock(&bm->data_block_lock);
	return res;
}


uint32_t L2PBm_get_free_block_num(L2P_bm *bm){
	uint32_t total_free_page_num=bm->segment_manager->total_free_page_num(bm->segment_manager, NULL);
	uint32_t total_free_block_num=total_free_page_num/BPS;
	total_free_block_num+=BPS-(bm->now_block_idx==NO_SEG?BPS:bm->now_block_idx);
	return total_free_block_num;
}

uint32_t L2PBm_pick_empty_RPBA(L2P_bm *bm){
	if(bm->reserve_block_idx==BPS){
		EPRINT("block over flow error", true);
	}
	return bm->reserve_seg->seg_idx *_PPS+
		(bm->reserve_block_idx++)*_PPB;
}

uint32_t target_PBA=UINT32_MAX;
extern bool debug_flag;
void L2PBm_make_map(L2P_bm *bm, uint32_t PBA, uint32_t sid, 
		uint32_t intra_idx){
	if(bm->PBA_map[PBA/_PPB].sid!=NO_MAP){
		EPRINT("the mapping should be empty", true);
	}
	if(PBA/_PPB==test_piece_ppa/L2PGAP/_PPB){
		printf("%u target populate\n", PBA/_PPB);
	}
	bm->PBA_map[PBA/_PPB].sid=sid;
	bm->PBA_map[PBA/_PPB].intra_idx=intra_idx;
	bm->PBA_map[PBA/_PPB].type=LSM_BLOCK_NORMAL;
}

void L2PBm_make_map_mixed(L2P_bm *bm, uint32_t PBA, uint32_t sid, uint32_t intra_idx, uint64_t frag_info){
	if(bm->PBA_map[PBA/_PPB].sid!=NO_MAP){
		EPRINT("the mapping should be empty", true);
	}
	bm->PBA_map[PBA/_PPB].sid=sid;
	bm->PBA_map[PBA/_PPB].intra_idx=intra_idx;
	bm->PBA_map[PBA/_PPB].type=LSM_BLOCK_MIXED;
	bm->PBA_map[PBA/_PPB].frag_info=frag_info;
}

void L2PBm_move_owner(L2P_bm *bm, uint32_t PBA, uint32_t sid, 
uint32_t intra_idx, uint32_t type){
	if(bm->PBA_map[PBA/_PPB].sid==NO_MAP){
		EPRINT("the mapping should be assigned", true);
	}
	if(bm->PBA_map[PBA/_PPB].type!=LSM_BLOCK_NORMAL && bm->PBA_map[PBA/_PPB].type!=LSM_BLOCK_MIXED){
		EPRINT("this block should be normal/mixed block", true);
	}
	if(PBA==target_PBA){
		printf("move %u:%u -> %u:%u\n", bm->PBA_map[PBA/_PPB].sid, bm->PBA_map[PBA/_PPB].intra_idx, sid, intra_idx);
	}

	bm->PBA_map[PBA/_PPB].sid=sid;
	bm->PBA_map[PBA/_PPB].intra_idx=intra_idx;
	bm->PBA_map[PBA/_PPB].type=type;
}

void L2PBm_block_fragment(L2P_bm *bm, uint32_t PBA, uint32_t sid){
	uint32_t bidx=PBA/_PPB;
	if(bm->PBA_map[bidx].type!=LSM_BLOCK_MIXED){
		if (bidx == test_piece_ppa / L2PGAP / _PPB)
		{
			if ((bm->PBA_map[bidx].frag_info & (1 << sid)) == 0)
			{
				printf("%u fragment, sid:%u\n", bidx, sid);
			}
		}
		bm->PBA_map[bidx].sid = NO_MAP;
		bm->PBA_map[bidx].intra_idx = NO_MAP;
		bm->PBA_map[bidx].type = LSM_BLOCK_FRAGMENT;
	}
	bm->PBA_map[bidx].frag_info|=(1<<sid);
}

void L2PBm_block_mixed_check_and_set(L2P_bm *bm, uint32_t PBA, uint32_t sid){
	uint32_t bidx=PBA/_PPB;
	if(bm->PBA_map[bidx].type==LSM_BLOCK_NORMAL){
		bm->PBA_map[bidx].type=LSM_BLOCK_MIXED;
	}
	bm->PBA_map[bidx].frag_info|=(1<<sid);
}

void check_block_sanity(uint32_t piece_ppa){
	uint32_t bidx=piece_ppa/L2PGAP/_PPB;
	if(bm_master->PBA_map[bidx].sid==NO_MAP){
		EPRINT("????????", true);
	}
	if(bm_master->segment_manager->is_invalid_piece(bm_master->segment_manager, piece_ppa)){
		EPRINT("????????", true);
	}
}

uint32_t L2PBm_get_map_ppa(L2P_bm *bm){
	fdriver_lock(&bm->map_block_lock);
	blockmanager *sm=bm->segment_manager;
	if(sm->check_full(bm->now_summary_seg)){
		if(sm->is_gc_needed(sm)){
			if(gc(bm, SUMMARY_SEG)==GC_COPY){
				if(sm->check_full(bm->now_summary_seg)){
					EPRINT("not effective gc on summary", true);
				}
				goto out;
			}
			//gcing all invalid target
		}
		bm->now_summary_seg=sm->get_segment(sm, BLOCK_ACTIVE);
		L2PBm_set_seg_type(bm, bm->now_summary_seg->seg_idx, SUMMARY_SEG);
	}
out:

	uint32_t res= bm->segment_manager->get_page_addr(bm->now_summary_seg);
	bm->PBA_map[res/_PPB].type=LSM_BLOCK_SUMMARY;
	fdriver_unlock(&bm->map_block_lock);
	return res;
}

uint32_t L2PBm_get_map_rppa(L2P_bm *bm){
	blockmanager *sm=bm->segment_manager;
	if(sm->check_full(bm->reserve_summary_seg)){
		EPRINT("reserve full error", true);
	}
	uint32_t res= bm->segment_manager->get_page_addr(bm->reserve_summary_seg);
	bm->PBA_map[res/_PPB].type=LSM_BLOCK_SUMMARY;
	return res;
}


uint32_t L2PBm_get_map_ppa_mixed(L2P_bm *bm){
	blockmanager *sm=bm->segment_manager;
	uint32_t res;
	if(sm->check_full(bm->now_summary_seg)){
		res=sm->get_page_addr(bm->reserve_summary_seg);
	}
	else{
		res=sm->get_page_addr(bm->now_summary_seg);
	}
	bm->PBA_map[res/_PPB].type=LSM_BLOCK_SUMMARY;
	return res;
}

void L2PBm_gc_lock(L2P_bm *bm, uint32_t bidx){
	bm->gc_lock[bidx]=true;
}


void L2PBm_gc_unlock(L2P_bm *bm, uint32_t bidx){
	bm->gc_lock[bidx]=false;
}
