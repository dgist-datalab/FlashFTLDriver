#ifndef __BMM_H__
#define __BMM_H__
#include "../include/container.h"
#include "./sequential/seq_block_manager.h"
#include "../include/debug_utils.h"
#include <stdio.h>
#include <stdlib.h>

enum{
	SEQ_BM,
};

//[seg#, page(8), chip(3), bus(3), card(1)] type(bit)

typedef struct horizontal_block_group{
	__block *block_set[_NOS];
	void *private_data;
}horizontal_block_group;

typedef struct blockmanager_master{
	__block  total_block_set[_NOB];
	horizontal_block_group h_block_group[BPS];
}blockmanager_master;


/*
 * Function: blockmanager_factory
 * ------------------------------
 *		return blockmanager type
 *
 *	type: target type
 *	li: dev_information for trim operation
 * */
struct blockmanager * blockmanager_factory(uint32_t type, lower_info *li);

/*
 * Function: blockmanager_free
 * -----------------------------
 *		deallocate blockmanager
 *
 *	bm:
 * */
void blockmanager_free(struct blockmanager *bm);

/*
 * Function: blockmanager_master_dump
 * -------------------------------
 *		dump data to fp
 *
 *  fp:
 * */
void blockmanager_master_dump(FILE *fp);

/*
 * Function: blockmanager_master_load
 * -------------------------------
 *		dump data from fp
 *  fp:
 * */
void blockmanager_master_load(FILE *fp);

/*
 * Function: block_reinit
 * ---------------------
 *		reinit block
 *
 *  b:target block
 * */
static inline void block_reinit(__block *b){
	//b->invalidate_piece_num=b->validate_piece_num=0;
	//b->seg_idx=0;
	b->now_assigned_pptr=0;
	memset(b->bitset, 0, _PPB*L2PGAP/8);
	memset(b->oob_list, 0 , sizeof(b->oob_list));
	b->invalidate_piece_num=b->validate_piece_num=0;
	b->is_full_invalid=false;
}

/*
 * Function: block_bit_set, query, unset
 * ----------------------
 *		1. set bitmap of block 2. return true if the bit is set 3. unset bitmap
 *	b:
 *	intra_offset:intra logical page offset
 *
 * */
static inline void block_bit_set(__block *b, uint32_t intra_offset){
	b->validate_piece_num++;
	b->bitset[intra_offset/8]|=(1<<(intra_offset%8));
}
static inline bool block_bit_query(__block *b, uint32_t intra_offset){
	return b->bitset[intra_offset/8]&(1<<(intra_offset%8));
}
static inline void block_bit_unset(__block *b, uint32_t intra_offset){
	b->invalidate_piece_num++;
	b->bitset[intra_offset/8]&=~(1<<(intra_offset%8));
}

static inline void blockmanager_full_invalid_check(__block *b){
	if(b->invalidate_piece_num && b->invalidate_piece_num==b->validate_piece_num){
		b->is_full_invalid=true;
	}
}


/*
 * Function: blockmanager_master_get_block
 * --------------------------------------
 *		return block from specialized function
 *
 *	horizontal_block_gid: target horizontal_block_gid
 *	get_block: segment type specialized function
 * */
__block *blockmanager_master_get_block(uint32_t horizontal_block_gid, 
		__block *(*get_block)(void *));

/*
 * Function: blockmanager_master_init
 * ---------------------------------
 *		initialize blockmanager_master
 * */
void blockmanager_master_init();

/*
 * Function: blockmanager_master_free
 * ---------------------------------
 *		deallocate blockmanager_master
 * */
void blockmanager_master_free();

bool default_check_full(__segment *s);
#endif
