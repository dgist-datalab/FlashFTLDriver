#ifndef _PAGE_H_
#define _PAGE_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "../../interface/interface.h"
#include "../../include/container.h"
#include "../blockmanager/BM.h"

//algo end req type definition.
#define TYPE uint8_t
#define DATA_R 0
#define DATA_W 1
#define GC_R 2
#define GC_W 3

typedef struct mapping_table{
	int32_t ppa;
} TABLE;

typedef struct page_OOB{
	int32_t lpa;
} P_OOB;

typedef struct SRAM{
	P_OOB OOB_RAM;
	PTR PTR_RAM;
} SRAM;

typedef struct pbase_params{
	value_set *value;
	TYPE type;
} pbase_params;

extern algorithm algo_pbase;

extern b_queue *free_b;
extern Heap *b_heap;

extern TABLE *page_TABLE;  // mapping Table.
extern uint8_t *VBM;	   //valid bitmap.
extern P_OOB *page_OOB;	   // Page level OOB.

extern Block *block_array;
extern Block *reserved;

extern int32_t _g_nop;
extern int32_t _g_nob;
extern int32_t _g_ppb;

extern int32_t gc_load;
extern int32_t gc_count;

//page.c
uint32_t pbase_create(lower_info*,algorithm *);
void *pbase_end_req(algo_req*);
void pbase_destroy(lower_info*, algorithm *);
uint32_t pbase_get(request* const);
uint32_t pbase_set(request* const);
uint32_t pbase_remove(request* const);

//page_utils.c
algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req);
value_set* SRAM_load(SRAM* sram, int32_t ppa, int idx); // loads info on SRAM.
void SRAM_unload(SRAM* sram, int32_t ppa, int idx); // unloads info from SRAM.
int32_t alloc_page();

//page_gc.c
int32_t pbase_garbage_collection(); // page- GC function.
 
#endif //!_PAGE_H_
