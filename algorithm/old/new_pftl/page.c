#include "page.h"
#include "../../bench/bench.h"

struct algorithm algo_pbase={
	.create = pbase_create,
	.destroy = pbase_destroy,
	.get = pbase_get,
	.set = pbase_set,
	.remove = pbase_remove
};

//heap globals.
b_queue *free_b;
Heap *b_heap;

TABLE *page_TABLE; //mapping table.
P_OOB *page_OOB;   //OOB area.
uint8_t *VBM;      //Valid Bitmap.

//blockmanager globals.
Block *block_array; //empty array.
Block *reserved;    //reserved.

//global for macro.
int32_t _g_nop;
int32_t _g_nob;
int32_t _g_ppb;

int32_t gc_load;
int32_t gc_count;

uint32_t pbase_create(lower_info* li, algorithm *algo){
	/*
	   initializes table, oob and blockmanager.
	   alloc globals for macro.
	   init heap.
	*/
	_g_nop = _NOP;
	_g_nob = _NOS;
	_g_ppb = _PPS;
	gc_count = 0;

	//printf("number of block: %d\n", _g_nob);
	//printf("page per block: %d\n", _g_ppb);
	//printf("number of page: %d\n", _g_nop);

	page_TABLE = (TABLE*)malloc(sizeof(TABLE) * _g_nop);
	page_OOB = (P_OOB*)malloc(sizeof(P_OOB) * _g_nop);
	VBM = (uint8_t*)malloc(_g_nop);
	algo->li = li;

	for(int i=0;i<_g_nop;i++){
     	page_TABLE[i].ppa = -1;
		page_OOB[i].lpa = -1;
		VBM[i] = 0;
	}//init table, oob and vbm.

	BM_Init(&block_array);
	reserved = &block_array[0];

	BM_Queue_Init(&free_b);
	for(int i=1;i<_g_nob;i++){
		BM_Enqueue(free_b, &block_array[i]);
	}
	b_heap = BM_Heap_Init(_g_nob - 1);//total size == NOB - 1.
	return 0;
}

void pbase_destroy(lower_info* li, algorithm *algo){
	/*
	 * frees allocated mem.
	 * destroys blockmanager.
	 */
	printf("gc count: %d\n", gc_count);
	BM_Queue_Free(free_b);
	BM_Heap_Free(b_heap);
	BM_Free(block_array);
	free(VBM);
	free(page_OOB);
	free(page_TABLE);
}

void *pbase_end_req(algo_req* input){
	/*
	 * end req differs according to type.
	 * frees params and input.
	 * in some case, frees inf_req.
	 */
	pbase_params *params = (pbase_params*)input->params;
	value_set *temp_set = params->value;
	request *res = input->parents;

	switch(params->type){
		case DATA_R:
			if(res){
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
	algo_pbase.li->pull_data(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_R, NULL, req));
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
	algo_pbase.li->push_data(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_W, NULL, req));
	if(page_TABLE[lpa].ppa != -1){//already mapped case.(update)
		VBM[page_TABLE[lpa].ppa] = 0;
		block_array[page_TABLE[lpa].ppa/_g_ppb].Invalid++;
	}
	page_TABLE[lpa].ppa = ppa;
	VBM[ppa] = 1;
	page_OOB[ppa].lpa = lpa;
	return 0;
}

uint32_t pbase_remove(request* const req){
	/*reset info. not being used now. */

	int32_t lpa;

	bench_algo_start(req);
	lpa = req->key;
	page_TABLE[lpa].ppa = -1; //reset to default.
	page_OOB[lpa].lpa = -1; //reset reverse_table to default.
	bench_algo_end(req);
	return 0;
}
