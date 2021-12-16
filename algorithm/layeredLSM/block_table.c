#include "block_table.h"
#define NO_MAP UINT32_MAX
#define NO_SEG UINT32_MAX

L2P_bm *L2PBm_init(blockmanager *sm){
	L2P_bm *res=(L2P_bm*)calloc(1, sizeof(L2P_bm));
	res->PBA_map=(uint32_t*)malloc(_NOB * sizeof(uint32_t));
	memset(res->PBA_map, -1, _NOB*sizeof(uint32_t));

	res->now_seg_idx=NO_SEG;
	res->total_seg_num=_NOS;
	res->seg_trimed_block_num=(uint32_t*)calloc(_NOS, sizeof(uint32_t));
	res->seg_block_bit=(bitmap**)calloc(_NOS, sizeof(bitmap*));
	for(uint32_t i=0; i<res->total_seg_num; i++){
		res->seg_block_bit[i]=bitmap_init(BPS);
	}
	res->segment_manager=sm;

	res->reserve_seg=sm->get_segment(sm, BLOCK_RESERVE);
	res->reserve_summary_seg=sm->get_segment(sm, BLOCK_RESERVE);
	res->now_summary_seg=sm->get_segment(sm, BLOCK_ACTIVE);
	return res;
}


void L2PBm_free(L2P_bm *bm){
	free(bm->PBA_map);
	free(bm->seg_trimed_block_num);
	for(uint32_t i=0; i<_NOS; i++){
		bitmap_free(bm->seg_block_bit[i]);
	}
	free(bm->seg_block_bit);
	free(bm);
}

void L2PBm_invalidate_PBA(L2P_bm *bm, uint32_t PBA){
	uint32_t seg_idx=PBA/_PPS;
	uint32_t intra_offset=PBA%BPS;
	bitmap_set(bm->seg_block_bit[seg_idx], intra_offset);
	bm->seg_trimed_block_num++;

	bm->PBA_map[PBA/_PPB]=NO_MAP;

	if(bm->seg_trimed_block_num[seg_idx]==BPS){
		/*
		__segment *target_seg=bm->segment_manager->retrieve_segment(bm->segment_manager, seg_idx);
		bm->segment_manager->trim_segment_force(bm->segment_manager, target_seg, bm->segment_manager->li);
		*/
		bitmap_reinit(bm->seg_block_bit[seg_idx], BPS);
		bm->seg_trimed_block_num=0;
	}

}

uint32_t L2PBm_pick_empty_PBA(L2P_bm *bm){
	if(bm->now_seg_idx==NO_SEG){
		//DEBUG_CNT_PRINT(test, 2, __FUNCTION__, __LINE__);
		__segment *seg=bm->segment_manager->get_segment(bm->segment_manager, BLOCK_ACTIVE);
		if(seg==NULL){
			EPRINT("gc not implemented!", true);
		}
		bm->now_seg_idx=seg->seg_idx;
		bm->now_block_idx=0;
	}
	
	uint32_t res=bm->now_seg_idx*_PPS+(bm->now_block_idx++)*_PPB;
	if(bm->now_block_idx==BPS){
		bm->now_seg_idx=NO_SEG;
	}
	return res;
}

void L2PBm_make_map(L2P_bm *bm, uint32_t PBA, uint32_t sid){
	if(bm->PBA_map[PBA/_PPB]!=NO_MAP){
		EPRINT("the mapping should be empty", true);
	}
	bm->PBA_map[PBA/_PPB]=sid;
}


uint32_t L2PBm_get_map_ppa(L2P_bm *bm){
	blockmanager *sm=bm->segment_manager;
	if(sm->check_full(bm->now_summary_seg)){
		if(sm->is_gc_needed(sm)){
			EPRINT("GC NEED", false);
		}
		bm->now_summary_seg=sm->get_segment(sm, BLOCK_ACTIVE);
	}

	return bm->segment_manager->get_page_addr(bm->now_summary_seg);
}
