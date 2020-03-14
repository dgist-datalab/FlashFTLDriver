#ifndef __H_DFTL__
#define __H_DFTL__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "../../interface/interface.h"
#include "../../interface/queue.h"
#include "../../include/container.h"
#include "../../include/dftl_settings.h"
#include "../../include/dl_sync.h"
#include "../../include/types.h"
#ifdef W_BUFF
#include "../Lsmtree/skiplist.h"
#endif
#include "../blockmanager/BM.h"
#include "lru_list.h"
#include "../../include/data_struct/redblack.h"

// Storing the key(or fingerprint(hash) of the key) in the mapping entry
#define STORE_KEY
#define USE_FINGERPRINT 1

// Support variable-sized value. Grain entries of the mapping table as GRAINED_UNIT
#define GRAINED_UNIT ( PIECE )
#define VAR_VALUE_MIN ( MINVALUE )
#define VAR_VALUE_MAX ( PAGESIZE )
#define GRAIN_PER_PAGE ( PAGESIZE / GRAINED_UNIT )

#define TYPE uint8_t
#define DATA_R DATAR
#define DATA_W DATAW
#define MAPPING_R MAPPINGR  // async
#define MAPPING_W MAPPINGW
#define MAPPING_M ( MAPPINGR | 0x80 )  // polling
#define TGC_R GCMR
#define TGC_W GCMW
#define TGC_M ( GCMR | 0x80 )
#define DGC_R GCDR
#define DGC_W GCDW

#ifdef STORE_KEY
#if USE_FINGERPRINT
#define ENTRY_SIZE (4+4)
#else
#define ENTRY_SIZE (4+256)
#endif
#else
#define ENTRY_SIZE (4)
#endif

#define EPP (PAGESIZE / ENTRY_SIZE) //Number of table entries per page
#define D_IDX (lpa / EPP)   // Idx of directory table
#define P_IDX (lpa % EPP)   // Idx of page table

#define CLEAN 0
#define DIRTY 1

#define HASH_KEY_INITIAL 0
#define HASH_KEY_NONE    1
#define HASH_KEY_SAME    2
#define HASH_KEY_DIFF    3

struct hash_params {
	uint32_t hash;
#if defined(STORE_KEY) && USE_FINGERPRINT
	uint32_t key_fp;
#endif
	int cnt;
	int find;

	int32_t hash_key;
};

// Page table data structure
typedef struct demand_mapping_table{
    int32_t ppa; //Index = lpa
#ifdef STORE_KEY
#if USE_FINGERPRINT
	uint32_t key_fp;
#else
	KEYT key;
#endif
#endif
} D_TABLE;

// Cache mapping table data strcuture
typedef struct cached_table{
    int32_t t_ppa;
    int32_t idx;
    D_TABLE *p_table;
    //value_set *p_table_vs;
    //NODE *queue_ptr; // for dirty pages (or general use)
#if C_CACHE
    NODE *clean_ptr; // for clean pages
#endif
    bool state;
	bool flying;
	bool wflying;
    request **flying_arr;
    int32_t num_waiting;
	snode **flying_snodes;
	int32_t num_snode;

	uint32_t read_hit;
	uint32_t write_hit;

    uint32_t dirty_cnt;
    bool *dirty_bitmap;
} C_TABLE;

// OOB data structure
typedef struct demand_OOB{
#ifdef DVALUE
	int32_t lpa[GRAIN_PER_PAGE];
#else
	int32_t lpa;
#endif
} D_OOB;

// SRAM data structure (used to hold pages temporarily when GC)
typedef struct demand_SRAM{
    int32_t origin_ppa;
    D_OOB OOB_RAM;
    int32_t *DATA_RAM;
} D_SRAM;

typedef struct demand_params{
    value_set *value;
    dl_sync dftl_mutex;
    TYPE type;
	snode *sn;
#ifdef DVALUE
	int offset;
#endif
} demand_params;

typedef struct read_params{
    int32_t t_ppa;
    uint8_t read;
} read_params;

typedef struct mem_table{
    D_TABLE *mem_p;
} mem_table;

/* extern variables */
extern algorithm __demand;

extern LRU *lru;

extern b_queue *free_b;
extern Heap *data_b;
extern Heap *trans_b;

extern C_TABLE *CMT; // Cached Mapping Table
extern uint8_t *VBM;
extern mem_table *mem_arr;
extern b_queue *mem_q;
extern D_OOB *demand_OOB; // Page level OOB

extern BM_T *bm;
extern Block *t_reserved;
extern Block *d_reserved;

extern volatile int32_t trans_gc_poll;
extern volatile int32_t data_gc_poll;

extern Redblack rb_tree;
extern pthread_mutex_t rb_lock;
extern pthread_mutex_t cpl_lock;
extern queue *range_q;

extern int32_t num_page;
#ifdef VARIABLE_VALUE
extern int32_t num_grain;
#endif
extern int32_t num_block;
extern int32_t p_p_b;
extern int32_t num_tpage;
extern int32_t num_tblock;
extern int32_t num_dpage;
extern int32_t num_dblock;
extern int32_t max_cache_entry;
extern int32_t num_max_cache;
extern int32_t max_clean_cache;
extern int32_t max_dirty_cache;

extern int32_t num_caching;

extern int32_t tgc_count;
extern int32_t dgc_count;
extern int32_t read_tgc_count;
extern int32_t tgc_w_dgc_count;
/* extern variables */

//dftl.c
uint32_t demand_create(lower_info*,blockmanager *, algorithm*);
void demand_destroy(lower_info*, algorithm*);
void *demand_end_req(algo_req*);
uint32_t demand_set(request *const);
uint32_t demand_get(request *const);
uint32_t demand_remove(request *const);
uint32_t __demand_set(request *const);
uint32_t __demand_get(request *const);
uint32_t __demand_remove(request *const);
uint32_t demand_eviction(request *const, char, bool*, bool*, snode *);

//dftl_range.c
uint32_t demand_range_query(request *const);
bool range_end_req(request *);

//dftl_utils.c
algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req);
D_TABLE* mem_deq(b_queue *q);
void mem_enq(b_queue *q, D_TABLE *input);
void merge_w_origin(D_TABLE *src, D_TABLE *dst);
//int lpa_compare(const void *a, const void *b);
int32_t tp_alloc(char, bool*);
int32_t dp_alloc();
value_set* SRAM_load(D_SRAM* d_sram, int32_t ppa, int idx, char t);
void SRAM_unload(D_SRAM* d_sram, int32_t ppa, int idx, char t);
void cache_show(char* dest);

//garbage_collection.c
int32_t tpage_GC();
int32_t dpage_GC();

#endif
