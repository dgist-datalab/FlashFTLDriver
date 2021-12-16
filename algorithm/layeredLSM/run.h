#ifndef __RUN_H__
#define __RUN_H__
#include "./sorted_table.h"
#include "./mapping_function.h"
#include "./page_aligner.h"
#include "./shortcut.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct run{
	uint32_t max_entry_num;
	uint32_t now_entry_num;
	struct shortcut_info *info;
	struct map_function *mf;

	uint32_t validate_piece_num;
	uint32_t invalidate_piece_num;

	pp_buffer *pp;
	st_array *st_body;
}run;

/*
 * Function: run_init
 * ------------------
 *		return new run 
 *
 * map_type: type of mapping_function
 * entry_num: number of entry in the run
 * fpr: target fpr of mapping function
 * bm: blockmanager for st_array
 * */
static inline run *run_init(uint32_t map_type, uint32_t entry_num, float fpr, L2P_bm *bm){
	run *res=(run*)malloc(sizeof(run));
	res->max_entry_num=entry_num;
	res->now_entry_num=0;
	res->mf=map_function_factory(map_type, entry_num, fpr, 48);
	res->pp=NULL;
	res->st_body=st_array_init(entry_num, bm);
	res->info=NULL;
	res->validate_piece_num=res->invalidate_piece_num=0;
	return res;
}

/*
 * Function: run_free
 * -----------------
 *		free allocated memory for run
 *
 * r:target run
 * */
static inline void run_free(run *r){
	if(r->pp){
		EPRINT("what happened?", true);
	}
	r->mf->free(r->mf);
	st_array_free(r->st_body);
	free(r);
}

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
 * data: target data
 * */
bool run_insert(run *r, uint32_t lba, char *data);

/*
 * Fucntion: run_insert_done
 * -------------------
 *		finishing the insert operation
 * r
 * */
void run_insert_done(run *r);
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
uint32_t run_translate_intra_offset(run *r, uint32_t intra_offset);

//#################################### run_query.c done

//#################################### run_util.c
void run_print(run *r);
//#################################### run_util.c done
//

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
run *run_merge(uint32_t run_num, run **rset, uint32_t map_type, float fpr, L2P_bm *bm);
//################################### run_merge.c done

#endif
