#ifndef PIECE_PPA_H
#define PIECE_PPA_H
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./block_table.h"
#include "./debug.h"

enum{BIT_ERROR, BIT_SUCCESS};
//static uint32_t test_piece_ppa=3740162;
//static uint32_t test_piece_ppa=818227;
static uint32_t test_piece_ppa=UINT32_MAX;

static void __debug_check(blockmanager *bm, uint32_t piece_ppa, 
		bool valid, bool map){
	if(test_piece_ppa==UINT32_MAX) return;
	if(test_piece_ppa==piece_ppa ||  piece_ppa==5049858){
		static int cnt=0;
		uint32_t lba=print_lba_from_piece_ppa(bm, piece_ppa);
		printf("%u %u lba=%u: (map:%u)",++cnt, piece_ppa, lba, map);
		valid?printf("valid-\n"):printf("invalid-\n");
		//print_stacktrace(8);
	}
}

static inline bool validate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool force){
	__debug_check(bm, piece_ppa, true, false);
	if(!bm->bit_set(bm, piece_ppa) && force){
		return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool force){
	__debug_check(bm, piece_ppa, false, false);
	if(!bm->bit_unset(bm, piece_ppa) && force){
		return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool validate_ppa(blockmanager *bm, uint32_t ppa, bool force){
	for(uint32_t i=0; i<L2PGAP; i++){
		__debug_check(bm, ppa*L2PGAP+i, true, true);
		if(!bm->bit_set(bm, ppa*L2PGAP+i) && force){
			return BIT_ERROR;
		}
	}
	return BIT_SUCCESS;
}

static inline bool invalidate_ppa(blockmanager *bm, uint32_t ppa, bool force){
	for(uint32_t i=0; i<L2PGAP; i++){
		__debug_check(bm, ppa*L2PGAP+i, false, true);
		if(!bm->bit_unset(bm, ppa*L2PGAP+i) && force){
			return BIT_ERROR;
		}
	}
	return BIT_SUCCESS;
}
#endif
