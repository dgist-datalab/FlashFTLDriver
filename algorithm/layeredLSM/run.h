#ifndef __RUN_H__
#define __RUN_H__
#include "./sorted_table.h"
#include "./mapping_function.h"
#include "./page_aligner.h"
#include "./shortcut.h"
#include "lsmtree.h"
#include <stdlib.h>
#include <stdio.h>

enum{
	RUN_NORMAL, RUN_PINNING, RUN_LOG,
};

enum{
	READ_DONE, READ_RETRY, READ_NOT_FOUND
};
typedef struct run{
	uint32_t run_idx;
	uint32_t max_entry_num;
	uint32_t now_entry_num;
	uint32_t type;
	struct shortcut_info *info;

	map_function *run_log_mf;
	pp_buffer *pp;
	struct sorted_table_array *st_body;
	struct lsmtree *lsm;
	fdriver_lock_t lock;
}run;

/*
 * Function: run_init
 * ------------------
 *		return new run 
 *
 * run_idx: idx of the run
 * map_type: type of mapping_function
 * entry_num: number of entry in the run
 * fpr: target fpr of mapping function
 * bm: blockmanager for st_array
 * type: type of run, pinning run have psa in memory
 * */
run *run_factory(uint32_t run_idx, uint32_t map_type, uint32_t entry_num, float fpr, L2P_bm *bm, uint32_t type, struct lsmtree *lsm);

/*
 * Function: run_free
 * -----------------
 *		free allocated memory for run
 *
 * r:target run
 * */
void run_free(run *r,struct shortcut_master *sc);

/*
 * Function: run_is_full
 * -----------------
 *		test target run wheter it is full or not
 *
 * r:
 * */
static inline bool run_is_full(run *r){
	return r->now_entry_num==r->max_entry_num;
}

static inline bool run_is_empty(run *r){
	return r->now_entry_num==0;
}

/*
 * Function: run_extract_target
 * ------------------------
 *		return target run from request 
 *
 * req:
 * */
static run *run_extract_target(request *req){
	return ((map_read_param*)req->param)->r;
}

//#################################### run_insert.c
/*
 * Fucntion: run_insert
 * -------------------
 *		return true, if inserting success
 *		return false, if inserting fail
 *
 * r: target run to be inserted
 * lba: target lba
 * psa: target psa, it is valid when the run type is RUN_PINNING
 * data: target data
 * merge_insert: flag is set when the function is called in run_merge
 * */
bool run_insert(run *r, uint32_t lba, uint32_t psa, char *data,
 	bool merge_insert, struct shortcut_master *shortcut);

/*
 * Fucntion: run_insert_done
 * -------------------
 *		finishing the insert operation
 * r
 * merge_insert: flag is set when the function is called in run_merge
 * */
void run_insert_done(run *r, bool merge_insert);

void run_padding_current_block(run *r);
/*
void run_trivial_move_start(run *r, uint32_t PBA);
void run_trivial_move_insert(run *r, struct shortcut_master *shortcut, uint32_t lba, uint32_t psa_for_pinning);
void run_trivial_move_finish(run *r);
*/

void run_copy_ste_to(run *r, struct sorted_table_entry *ste, struct summary_page_meta *spm, map_function *mf, bool unlinked_data_copy);

void run_copy_unlinked_flag_update(run *r, uint32_t ste_num, bool flag, uint32_t original_level, uint32_t original_recency);

//#################################### run_insert.c done

//#################################### run_query.c
/*
 * Function: run_query
 * ------------------
 *		return psa
 *		if it cannot find psa of target lba, return NOT_FOUND
 *
 * r: queried run
 * lba: target lba
 * */
uint32_t run_query(run *r, request *req);

/*
 * Function: run_query_retry
 * ------------------------
 *		return psa for retring query
 *		if it cannot find psa of target lba, return NOT_FOUND
 *
 * r:queried run
 * lba: target_lba
 * prev_offset: previous information for query
 * oob_set: oob (lba set in physical_page) to adjust intra offset
 * */
uint32_t run_query_retry(run *r, request *req);

/*
 * Function: run_translate_intra_offset
 *------------------------------------
 *		return psa from intra offset
 *r:
 *intra_offset:
 * */
uint32_t run_translate_intra_offset(run *r, uint32_t ste_num, uint32_t intra_offset);


/*
 * Function: run_find_include_address
 *------------------------------------
 *		return run which includes target lba and psa
 *
 *sc: shortcut for fast finding
 *lba:
 *psa:
 *intra_offset:
 * */
run *run_find_include_address(struct shortcut_master *sc, uint32_t lba, uint32_t psa, uint32_t *_ste_num, 
uint32_t *intra_offset);

/*
 * Function: run_find_include_address_byself
 *------------------------------------
 *		return intra_offset of target lba and psa
 *
 *r: 
 *lba:
 *psa:
 * */
uint32_t run_find_include_address_byself(run *r, uint32_t lba, uint32_t psa, uint32_t *ste_num);

//#################################### run_query.c done

//#################################### run_util.c
void run_print(run *r, bool content);
uint64_t run_memory_usage(run *r, uint32_t target_bit);

//#################################### run_util.c done

//################################### run_merge.c 
/*
 * Function: run_merge
 * ------------------
 *		merging run
 *
 * run_num: total number of run to merge
 * rset: set of run which is sorted by version
 *
 * */
void run_merge(uint32_t run_num, run **rset,  run *target_run, struct lsmtree *lsm);

/*
 * Function: run_recontstruct
 * ------------------
 *		reconstruct run
 *
 * src:	
 * des:
 * */
void run_recontstruct(struct lsmtree *lsm, run *src, run *des);	
//################################### run_merge.c done
#endif