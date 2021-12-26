#ifndef PIECE_PPA_H
#define PIECE_PPA_H
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./block_table.h"
#include "./debug.h"

enum{BIT_ERROR, BIT_SUCCESS};
static uint32_t test_piece_ppa=UINT32_MAX;

static inline void __debug_check(blockmanager *bm, uint32_t piece_ppa, 
		bool valid){
	if(test_piece_ppa==UINT32_MAX) return;
	if(test_piece_ppa==piece_ppa){
		static int cnt=0;
		printf("%u lba=%u:",++cnt, print_lba_from_piece_ppa(bm, piece_ppa));
		valid?printf("valid-"):printf("invalid-");
		print_stacktrace(8);
	}
}

static inline bool validate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool force){
	__debug_check(bm, piece_ppa, true);
	if(!bm->bit_set(bm, piece_ppa) && force){
		return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool force){
	__debug_check(bm, piece_ppa, false);
	if(!bm->bit_unset(bm, piece_ppa) && force){
		return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool validate_ppa(blockmanager *bm, uint32_t ppa, bool force){
	for(uint32_t i=0; i<L2PGAP; i++){
		if(!bm->bit_set(bm, ppa*L2PGAP+i) && force){
			__debug_check(bm, ppa*L2PGAP+i, true);
			return BIT_ERROR;
		}
	}
	return BIT_SUCCESS;
}

static inline bool invalidate_ppa(blockmanager *bm, uint32_t ppa, bool force){
	for(uint32_t i=0; i<L2PGAP; i++){
		if(!bm->bit_unset(bm, ppa*L2PGAP+i) && force){
			__debug_check(bm, ppa*L2PGAP+i, false);
			return BIT_ERROR;
		}
	}
	return BIT_SUCCESS;
}
#endif
