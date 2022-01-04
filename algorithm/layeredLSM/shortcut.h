#ifndef SHORTCUT_H_
#define SHORTCUT_H_
#include "./run.h"
#include "../../include/sem_lock.h"
#include "../../include/debug_utils.h"
#include <list>
#include <stdlib.h>
#define NOT_ASSIGNED_SC UINT8_MAX
typedef struct shortcut_info{
	uint8_t idx;
	uint32_t level_idx;
	uint32_t recency;
	uint32_t linked_lba_num;
	uint32_t unlinked_lba_num;
	bool now_compaction;
	run *r;
}shortcut_info;
typedef struct shortcut_info sc_info;

typedef struct shortcut_master{
	std::list<uint32_t> *free_q;
	uint8_t *sc_map;
	sc_info *info_set;
	uint32_t max_shortcut_num;
	uint32_t now_recency;
	fdriver_lock_t lock;
}shortcut_master;

typedef shortcut_master sc_master;

/*
 * Function: shortcut_init
 * -----------------------
 *		return shortcut_master
 *
 *	max_shortcut_num: max number of shortcut
 *	lba_range: max number of lba
 * */
sc_master *shortcut_init(uint32_t max_shortcut_num, uint32_t lba_range);

/*
 * Function: shortcut_add_run
 * -------------------------
 *		assign new shortcut idx to run
 *
 *	sc:
 *	r:
 * */
void shortcut_add_run(sc_master *sc, run *r, uint32_t level_num);

/*
 * Function: __shortcut_add_run
 * -------------------------
 *		assign new shortcut idx to run in merge
 *		the recency of sc_info will be set the biggest recency in rset
 *
 *	sc:
 *	r: target run
 *	rset: target merged set 
 *	merge_num: the number of merged set
 * */
void shortcut_add_run_merge(sc_master *sc, run *r, run **rset, uint32_t merge_num);


/*
 * Function: __shortcut_validity_check_lba
 * ------------------------------------
 *			checking lba of run validity
 *
 *	sc:
 *	r: the run has lba
 *	lba: target lba
 * */
bool shortcut_validity_check_lba(sc_master *sc, run *r, uint32_t lba);

/*
 * Function: shortcut_link_lba
 * ------------------------------
 *		assign shortcut idx to lba
 *
 *	sc:
 *	r: the run which includes target lba
 *	lba:
 * */
void shortcut_link_lba(sc_master *sc, run *r, uint32_t lba);

/*
 * Function: shortcut_unlink_lba
 * ------------------------------
 *		unlink shortcut from lba
 *
 *	sc:
 *	r: the run which tries to exclude target lba
 *	lba:
 * */
void shortcut_unlink_lba(sc_master *sc, run *r, uint32_t lba);

/*
 * Function: shortcut_query
 * -----------------------
 *		return target run for query from lba
 *
 *	sc:
 *	lba
 * */
run* shortcut_query(sc_master *sc, uint32_t lba);

/*
 * Function: shortcut_unlink_and_link_lba
 * ------------------------------------
 *		find old sc_info and unlinked lba and link new lba to run
 * */
void shortcut_unlink_and_link_lba(sc_master *sc, run *r, uint32_t lba);

bool shortcut_validity_check_and_link(sc_master*sc, run *src_r, run* des_r, uint32_t lba);
/*
 * Function: shortcut_release_sc_info
 * ---------------------------------
 *		release sc_info idx
 *
 * idx:
 * */
void shortcut_release_sc_info(sc_master *sc, uint32_t idx);

/*
 * Function: shortcut_free
 * ----------------------
 *  sc:
 * */
void shortcut_free(sc_master *sc);

static inline void shortcut_set_compaction_flag(shortcut_info *info, bool compaction_flag){
	info->now_compaction=compaction_flag;
}

/* 
 * Function: shortcut_is_full
 * --------------------------
 *		return true, when the shortcut is full
 *
 * sc:
 * */
static inline bool shortcut_is_full(sc_master *sc){
	return sc->free_q->size()==0;
}

static inline bool shortcut_compaction_trigger(sc_master *sc){
	return sc->free_q->size()==1;
}

#endif
