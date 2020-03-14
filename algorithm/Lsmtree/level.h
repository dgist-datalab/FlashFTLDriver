#ifndef __H_LEVEL_H__
#define __H_LEVEL_H__
#include "../../include/settings.h"
#include "../../include/container.h"
#include "../../include/lsm_settings.h"
#include "bloomfilter.h"
#include "page.h"
#include "lsmtree.h"
#include "bloomfilter.h"
#include "cache.h"
#include <pthread.h>

typedef enum{
	NOTCOMP,
	COMP,
	READED,
	INVBYGC,
	SEQCOMP,
	ONELEV,
	SEQMOV,
}run_comp_type;

#define for_each_lev(run,iter,func) \
	for(run=func(iter);run!=NULL;run=func(iter))

typedef struct skiplist skiplist;

typedef struct keyset{
	ppa_t ppa;
	KEYT lpa;
}keyset;

typedef struct htable{
	keyset *sets;
	//	uint8_t *bitset;
	uint8_t iscached;//for compaction(partial_leveling)
	/*
	   when the cache is used for header reading in compaction, but It can be evicted by new inserted cache after compaction, so we should check it
	   0->not cached
	   1->cached but it is used in compaction
	   2->cached but it isn't used in compaction.

	 */
	char *nocpy_table;
	value_set *origin;
	uint8_t t_b;//0, MALLOC
	//1, valueset from W
	//2, valueset from R
	volatile uint8_t done;
}htable;

typedef struct htable_t{
	keyset sets[PAGESIZE/KEYSETSIZE];
	char *nocpy_table;
	value_set *origin;
}htable_t;

typedef struct run{ 
	KEYT key;
	KEYT end;
	ppa_t pbn;

	//for caching
	cache_entry *c_entry;
	char *cache_nocpy_data_ptr;
	htable *cache_data;

	htable *cpt_data;
	void *run_data;
	char *level_caching_data;
	char iscompactioning;

	volatile char isflying;
	void *from_req;
	void** waitreq;
	void **gc_waitreq;
	int wait_idx;
	int gc_wait_idx;
	char gc_should_write;
}run_t;

typedef struct pipe_line_run{
	fdriver_lock_t *lock;
	run_t *r;
}pl_run;

typedef struct level{
	//llog *h;
	int32_t idx,m_num,n_num;
	KEYT start,end;
	float fpr;
	bool iscompactioning;
	bool istier;
#ifdef BLOOM
	BF *filter;
#endif
	void* level_data;
}level;

typedef struct lev_iter{
	int lev_idx;
	KEYT from,to;
	void *iter_data;
}lev_iter;

typedef struct keyset_iter{
	void *private_data;
}keyset_iter;

typedef struct level_ops{
	/*level operation*/
	level* (*init)(int size, int idx, float fpr, bool istier);
	void (*release)( level*);
	run_t* (*insert)( level* des, run_t *r);
	void (*lev_copy)(level *des, level *src);
	keyset *(*find_keyset)(char *data,KEYT lpa);//find one
	uint32_t (*find_idx_lower_bound)(char *data,KEYT lpa);
	void (*find_keyset_first)(char *data,KEYT *des);
	void (*find_keyset_last)(char *data,KEYT *des);
	bool (*full_check)( level*);
	void (*tier_align)( level*);
	void (*move_heap)( level* des,  level *src);
	bool (*chk_overlap)( level *des, KEYT star, KEYT end);
	uint32_t (*range_find)( level *l,KEYT start, KEYT end,  run_t ***r);
	uint32_t (*range_find_compaction)( level *l,KEYT start, KEYT end,  run_t ***r);
	uint32_t (*unmatch_find)( level *,KEYT start, KEYT end, run_t ***r);
	run_t* (*next_run)(level *,KEYT key);
	lev_iter* (*get_iter)( level*,KEYT from, KEYT to); //from<= x <to
	lev_iter* (*get_iter_from_run)(level *,run_t *sr,run_t * er);
	run_t* (*iter_nxt)( lev_iter*);
	uint32_t (*get_number_runs)(level*);
	uint32_t (*get_max_table_entry)();
	uint32_t (*get_max_flush_entry)(uint32_t);

	keyset_iter *(*keyset_iter_init)(char *keyset_data, int from);
	keyset *(*keyset_iter_nxt)(keyset_iter*,keyset *target);
	/*compaciton operation*/
#ifdef BLOOM
	htable* (*mem_cvt2table)(skiplist *,run_t *,BF *);
#else
	htable* (*mem_cvt2table)(skiplist *,run_t *);
#endif

	void (*merger)( skiplist*, run_t** src,  run_t** org,  level *des);
	run_t *(*cutter)( skiplist *,  level* des, KEYT* start, KEYT* end);
	run_t *(*partial_merger_cutter)(skiplist*,pl_run *, pl_run *,uint32_t, uint32_t, level *,void*(*lev_insert_write)(level*, run_t*));
	void (*normal_merger)(skiplist *,run_t *t_run, bool);
//	run_t **(*normal_cutter)(skiplist *,KEYT, bool just_one);
#ifdef BLOOM
	BF *(*making_filter)(run_t *, int num, float);
#endif

	/*run operation*/
	run_t*(*get_run_idx)(level *, int idx);
	run_t*(*make_run)(KEYT start, KEYT end, uint32_t pbn);
	run_t*(*find_run)( level*,KEYT lpa);
	run_t**(*find_run_num)( level*,KEYT lpa, uint32_t num);
	void (*release_run)( run_t *);
	run_t* (*run_cpy)( run_t *);

	/*mapping operation*/
	ppa_t (*moveTo_fr_page)(bool isgc);
	ppa_t (*get_page)(uint8_t plength, KEYT simul_key);
	bool (*block_fchk)();

	void (*range_update)(level *,run_t*,KEYT);
	int (*cache_comp_formatting)(level *,run_t ***,bool isnext_cache);
	keyset_iter* (*header_get_keyiter)(level *, char *, KEYT *);
	keyset (*header_next_key)(level *, keyset_iter *);
	void (*header_next_key_pick)(level *, keyset_iter *, keyset *);
#ifdef KVSSD
	KEYT *(*get_lpa_from_data)(char *data,ppa_t simul_ppa, bool isheader);
#endif
	
	uint32_t (*get_level_mem_size)(level *);
	/*for debugging*/
	void (*checking_each_key)(char *data,void*(*test)(KEYT a,ppa_t ppa));
	void (*check_order)(level *);
	void (*print)( level*);
	void (*print_run)(run_t *);
	void (*print_level_summary)();
	void (*all_print)();
	void (*header_print)(char*);
}level_ops;

ppa_t def_moveTo_fr_page(bool isgc);
ppa_t def_get_page(uint8_t plegnth, KEYT simul_key);
bool def_blk_fchk();

void def_move_heap( level *des,  level *src);
run_t *def_make_run(KEYT start, KEYT ent, uint32_t pbn);
bool def_fchk( level *);
#endif
