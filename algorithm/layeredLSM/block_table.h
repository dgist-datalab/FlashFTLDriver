#ifndef BLOCK_TABLE_H
#define BLOCK_TABLE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <queue>

#include "../../include/data_struct/bitmap.h"
#include "../../include/container.h"
#include "../../include/settings.h"
#include "../../include/debug_utils.h"
#include "../../include/sem_lock.h"

#define MAX_RUN_NUM_FOR_FRAG 64 

enum{
	LSM_BLOCK_EMPTY, LSM_BLOCK_SUMMARY, LSM_BLOCK_NORMAL, LSM_BLOCK_FRAGMENT,
	LSM_BLOCK_MIXED,
};


enum{
	EMPTY_SEG, SUMMARY_SEG, DATA_SEG
};

typedef struct block_info{
	uint32_t sid;
	uint32_t intra_idx;
	uint32_t type;
	uint64_t frag_info;
}block_info;

typedef struct L2P_block_manager{
	block_info *PBA_map;				//run_chunk_id to physical block address 
	bool *gc_lock;

	uint32_t total_seg_num;			//max number of segemnt in Flash
	uint32_t *seg_trimed_block_num;	//the number of trimed block in a segment
	bitmap **seg_block_bit;			//the bitmap for checking trimed block in a segment

	uint32_t now_segment_idx;		//if this variable is set to UINT32_MAX, 
									//it needs to be assigend a new segment

	__segment* reserve_seg;
	__segment* reserve_summary_seg;
	__segment* now_summary_seg;
	
	uint32_t reserve_block_idx;
	uint32_t now_block_idx;
	uint32_t now_seg_idx;
	blockmanager *segment_manager; 
	uint32_t *seg_type;

	fdriver_lock_t data_block_lock;
	fdriver_lock_t map_block_lock;
}L2P_bm;


/*
	Function: L2PBm_init
	--------------------
		Returns a new L2PBm for managing block mapping
	
	sm: segment manager for get empty segment
	total_run_num: for check block frag_info 
 */
L2P_bm *L2PBm_init(blockmanager *sm, uint32_t total_run_num);

/*
	Function: L2PBm_free
	--------------------
		Free allocated memory
	
	bm: target L2PBm
 */
void L2PBm_free(L2P_bm *bm);

/*
	Function: L2PBm_invalidate_PBA
	--------------------
		invalidating PBA
	
	bm: L2PBm
	PBA: target PBA to be invalid
 */
void L2PBm_invalidate_PBA(L2P_bm *bm, uint32_t PBa);

/*
	Function: L2PBm_pick_empty_PBA
	--------------------
		returns empty physical block address
	
	bm: L2PBm
 */
uint32_t L2PBm_pick_empty_PBA(L2P_bm *bm);

/*
	Function: L2PBm_pick_empty_RPBA
	--------------------
		returns empty reserve physical block address
	
	bm: L2PBg
 */
uint32_t L2PBm_pick_empty_RPBA(L2P_bm *bm);

/*
	Function: L2PBm_make_map
	------------------------
		make mapping inforamtion between PBA -> sid
	
	bm:
	PBA: target PBA
	sid: id of st_array which has PBA
	intra_idx: the index of PBA in PBA list
 */
void L2PBm_make_map(L2P_bm *bm, uint32_t PBA, uint32_t sid, 
		uint32_t intra_idx);

void L2PBm_make_map_mixed(L2P_bm *bm, uint32_t PBA, uint32_t sid, uint32_t intra_idx, uint64_t frag_info);

void L2PBm_move_owner(L2P_bm *bm, uint32_t PBA, uint32_t sid, 
uint32_t intra_idx, uint32_t type);

/* 
 * Function: L2PBm_block_fragment
 * ------------------------------
 *		change lsm block type to LSM_BLOCK_FRAGMENT
 *	bm:
 *	ppa: target ppa
 * */
void L2PBm_block_fragment(L2P_bm *bm, uint32_t ppa, uint32_t sid);

void L2PBm_block_mixed_check_and_set(L2P_bm*bm, uint32_t PBA, uint32_t sid);

/*
 * Function:L2PBm_get_map_ppa
 * -------------------------
 *		return ppa for summary page
 *
 *	sid: managed sorted_array
 * */
uint32_t L2PBm_get_map_ppa(L2P_bm *bm);

/*
 * Function:L2PBm_get_map_ppa
 * -------------------------
 *		return ppa for summary page
 *
 *	sid: managed sorted_array
 * */
uint32_t L2PBm_get_map_rppa(L2P_bm *bm);

/*
 * Function:L2PBm_get_map_ppa_mixed
 * -------------------------
 *		return ppa for summary page
 *
 *	sid: managed sorted_array
 * */
uint32_t L2PBm_get_map_ppa_mixed(L2P_bm *bm);

uint32_t L2PBm_get_free_block_num(L2P_bm *bm);

void L2PBm_gc_lock(L2P_bm *bm, uint32_t bidx);
void L2PBm_gc_unlock(L2P_bm *bm, uint32_t bidx);

void check_block_sanity(uint32_t piece_ppa);

/*
 * Function:L2Pbm_set_seg_type
 * -------------------------
 *		return ppa for summary page
 *
 *	sid: managed sorted_array
 * */
static inline void L2PBm_set_seg_type(L2P_bm *bm,uint32_t seg_idx, uint32_t type){
	bm->seg_type[seg_idx]=type;
}

/*
 * Func
 *
 * */
static inline void L2PBm_clear_block_info(L2P_bm *bm, uint32_t bidx){
	bm->PBA_map[bidx].sid=UINT32_MAX;
	bm->PBA_map[bidx].intra_idx=UINT32_MAX;
	bm->PBA_map[bidx].type=LSM_BLOCK_EMPTY;
	bm->PBA_map[bidx].frag_info=0;
}

static inline bool L2PBm_is_nomral_block(L2P_bm *bm, uint32_t PBA){
	return bm->PBA_map[PBA/_PPB].type==LSM_BLOCK_NORMAL;
}

static inline uint32_t L2PBm_get_block_type(L2P_bm *bm, uint32_t PBA){
	return bm->PBA_map[PBA/_PPB].type;
}

static inline bool L2PBm_is_frag_block(L2P_bm *bm, uint32_t PBA){
	return bm->PBA_map[PBA/_PPB].type==LSM_BLOCK_FRAGMENT;
}

#endif
