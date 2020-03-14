#include "page.h"
#include "../../bench/bench.h"

struct algorithm algo_pbase={
	.create = pbase_create,
	.destroy = pbase_destroy,
	.read = pbase_get,
	.write = pbase_set,
	.remove = pbase_remove
};

//heap globals.
b_queue *free_b;
Heap *b_heap;

TABLE *page_TABLE; //mapping table.
P_OOB *page_OOB;   //OOB area.

//blockmanager globals.
BM_T *BM;
Block *reserved;    //reserved.

//global for macro.
int32_t _g_nop;
int32_t _g_nob;
int32_t _g_ppb;

int32_t gc_load;
int32_t gc_count;

#ifdef BUSE_MEASURE
MeasureTime algoTime;
MeasureTime algoendTime;
#endif

uint32_t pbase_create(lower_info* li, algorithm *algo){
	/*
	   initializes table, oob and blockmanager.
	   alloc globals for macro.
	   init heap.
	*/
#ifdef BUSE_MEASURE
    measure_init(&algoTime);
    measure_init(&algoendTime);
#endif
    printf("This is PAGE FTL\n");
	_g_nop = _NOP;
	_g_nob = _NOS;
	_g_ppb = _PPS;
	gc_count = 0;


	//printf("number of block: %d\n", _g_nob);
	//printf("page per block: %d\n", _g_ppb);
	//printf("number of page: %d\n", _g_nop);

	page_TABLE = (TABLE*)malloc(sizeof(TABLE) * _g_nop);
	page_OOB = (P_OOB*)malloc(sizeof(P_OOB) * _g_nop);
	algo->li = li;

	for(int i=0;i<_g_nop;i++){
     	page_TABLE[i].ppa = -1;
		page_OOB[i].lpa = -1;
	}//init table, oob and vbm.

	//BM_Init(&block_array);
	BM = BM_Init(_g_nob, _g_ppb, 1, 1);

	reserved = &BM->barray[0];
	BM_Queue_Init(&free_b);
	for(int i=1;i<_g_nob;i++){
		BM_Enqueue(free_b, &BM->barray[i]);
	}
	b_heap = BM_Heap_Init(_g_nob - 1);//total size == NOB - 1.
	
	BM->harray[0] = b_heap;
	BM->qarray[0] = free_b;
	return 0;
}

void pbase_destroy(lower_info* li, algorithm *algo){
	/*
	 * frees allocated mem.
	 * destroys blockmanager.
	 */
	printf("gc count: %d\n", gc_count);
	BM_Free(BM);
	free(page_OOB);
	free(page_TABLE);
#ifdef BUSE_MEASURE
    printf("algoTime : ");
    measure_adding_print(&algoTime);
    printf("algoendTime : ");
    measure_adding_print(&algoendTime);
#endif
}

void *pbase_end_req(algo_req* input){
	/*
	 * end req differs according to type.
	 * frees params and input.
	 * in some case, frees inf_req.
	 */
#ifdef BUSE_MEASURE
    if(((pbase_params*)input->params)->type==DATA_R)
        MS(&algoendTime);
#endif
	pbase_params *params = (pbase_params*)input->params;
	value_set *temp_set = params->value;
	request *res = input->parents;

	switch(params->type){
		case DATA_R:
			if(res){
#ifdef BUSE_MEASURE
                MA(&algoendTime);
#endif
				res->end_req(res);
			}
			break;
		case DATA_W:
			if(res){
				res->end_req(res);
			}
			break;
		case GC_R:
			gc_load++;	
			break;
		case GC_W:
			inf_free_valueset(temp_set, FS_MALLOC_W);
			break;
	}
	free(params);
	free(input);
	return NULL;
}

uint32_t pbase_get(request* const req){
	/*
	 * gives pull request to lower level.
	 * reads mapping data.
	 * !!if not mapped, does not pull!!
	 */
	int32_t lpa;
	int32_t ppa;

#ifdef BUSE_MEASURE
    MS(&algoTime);
#endif
	bench_algo_start(req);
	lpa = req->key;
	ppa = page_TABLE[lpa].ppa;
	if(ppa == -1){
		bench_algo_end(req);
		req->type = FS_NOTFOUND_T;
		req->end_req(req);
		return 1;
	}
	bench_algo_end(req);	
#ifdef BUSE_MEASURE
    MA(&algoTime);
#endif
	algo_pbase.li->read(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_R, NULL, req));
	return 0;
}

uint32_t pbase_set(request* const req){
	/*
	 * gives push request to lower level.
	 * write mapping data, AFTER push request.
	 * need to write OR update table, oob, VBM.
	 * if necessary, allocation may perf garbage collection.
	 */
	int32_t lpa;
	int32_t ppa;

	bench_algo_start(req);
	lpa = req->key;
	ppa = alloc_page();
	bench_algo_end(req);
	algo_pbase.li->write(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_W, NULL, req));
	if(page_TABLE[lpa].ppa != -1){//already mapped case.(update)
		BM_InvalidatePage(BM,page_TABLE[lpa].ppa);
	}
	page_TABLE[lpa].ppa = ppa;
	BM_ValidatePage(BM,ppa);
	page_OOB[ppa].lpa = lpa;
	return 0;
}

uint32_t pbase_remove(request* const req){
	/*reset info. not being used now. */

	int32_t lpa;

	bench_algo_start(req);
	lpa = req->key;
    if(page_TABLE[lpa].ppa == -1){
        req->end_req(req);
        return 0;
    }

    BM_InvalidatePage(BM,page_TABLE[lpa].ppa);
	page_TABLE[lpa].ppa = -1; //reset to default.
	page_OOB[lpa].lpa = -1; //reset reverse_table to default.
    req->end_req(req);
	bench_algo_end(req);
	return 0;
}
