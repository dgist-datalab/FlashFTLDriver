#include "../../include/container.h"

enum{BIT_ERROR, BIT_SUCCESS};

static inline bool validate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool force){
	if(!bm->bit_set(bm, piece_ppa) && force){
		return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa, bool force){
	if(!bm->bit_unset(bm, piece_ppa) && force){
		return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool validate_ppa(blockmanager *bm, uint32_t ppa, bool force){
	for(uint32_t i=0; i<L2PGAP; i++){
		if(!bm->bit_set(bm, ppa*L2PGAP+i) && force)
			return BIT_ERROR;
	}
	return BIT_SUCCESS;
}

static inline bool invalidate_ppa(blockmanager *bm, uint32_t ppa, bool force){
	for(uint32_t i=0; i<L2PGAP; i++){
		if(!bm->bit_unset(bm, ppa*L2PGAP+i) && force)
			return BIT_ERROR;
	}
	return BIT_SUCCESS;
}
