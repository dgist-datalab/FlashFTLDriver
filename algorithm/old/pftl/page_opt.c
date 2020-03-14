#include "page.h"

struct algorithm algo_pbase=
{
	.create = pbase_create,
	.destroy = pbase_destroy,
	.get = pbase_get,
	.set = pbase_set,
	.remove = pbase_remove
};

int64_t PPA_status = 0;//starts at block 1, not 0.
int64_t RSV_status = 0;//overprovision area.

TABLE *page_TABLE;
OOB *page_OOB;
SRAM *page_SRAM;


uint32_t pbase_create(lower_info* li, algorithm *algo) //define & initialize mapping table.
{
	algo->li = li; 	//allocate li parameter to algorithm's li.
	page_TABLE = (TABLE*)malloc(sizeof(TABLE)*_NOP);
	for(int i = 0; i < _NOP; i++)
     	page_TABLE[i].lpa_to_ppa = -1;
	
	page_OOB = (OOB *)malloc(sizeof(OOB)*_NOP);
	for (int i = 0; i < _NOP; i++)
		page_OOB[i].reverse_table = -1;

	page_SRAM = (SRAM*)malloc(sizeof(SRAM)*_PPB);
	for (int i = 0; i<_PPB; i++)
	{
		page_SRAM[i].lpa_RAM = -1;
		page_SRAM[i].VPTR_RAM = NULL;
	}

	BM_Init();
	printf("pbase_create done!\n");
	return 0;
	//init mapping table.
}	//now we can use page table after pbase_create operation.



void pbase_destroy(lower_info* li, algorithm *algo)
{					  
	free(page_OOB);
	free(page_SRAM);
	free(page_TABLE);
	BM_shutdown();
}

void *pbase_end_req(algo_req* input)
{
	request *res=input->parents;
	res->end_req(res);//call end_req of parent req.
	free(input); //free target algo_req.
	return 0;
}

void *pbase_algo_end_req(algo_req* input)
{
	_g_count++;
	//printf("global count : %d\n",_g_count);
	free(input);
	return 0;
}

uint32_t pbase_get(request* const req)
{
	//put request in normal_param first.
	//request has a type, key and value.
	int target;
	algo_req *my_req;

	bench_algo_start(req);
	target = page_TABLE[req->key].lpa_to_ppa;
	if (target == -1){
		req->type = FS_NOTFOUND_T;
		bench_algo_end(req);	
		req->end_req(req);
		return 0;
	}
	my_req = (algo_req*)malloc(sizeof(algo_req)); //init reqeust
	my_req->parents = req;
	my_req->end_req = pbase_end_req; //allocate end_req for request.
	bench_algo_end(req);
	algo_pbase.li->pull_data(target, PAGESIZE, req->value, ASYNC, my_req);
	//key-value operation.
	return 0;
}

uint32_t pbase_set(request* const req){
	int temp;
	algo_req *my_req;

	bench_algo_start(req);
	my_req = (algo_req*)malloc(sizeof(algo_req));
	my_req->parents = req;
	my_req->end_req = pbase_end_req;

	PPA_status = alloc_page();
	if (PPA_status == -1)
		PPA_status = pbase_garbage_collection();

	if (page_TABLE[req->key].lpa_to_ppa != -1){
		temp = page_TABLE[req->key].lpa_to_ppa; //find old ppa.
		BM_invalidate_ppa(blockArray,temp);
	}
	
	page_TABLE[req->key].lpa_to_ppa = PPA_status; //map ppa status to table.
	BM_validate_ppa(blockArray,PPA_status);
	page_OOB[PPA_status].reverse_table = req->key;//reverse-mapping.
	bench_algo_end(req);	
	algo_pbase.li->push_data(PPA_status++,PAGESIZE,req->value,ASYNC,my_req);
	return 0;
}

uint32_t pbase_remove(request* const req){
	page_TABLE[req->key].lpa_to_ppa = -1; //reset to default.
	page_OOB[req->key].reverse_table = -1; //reset reverse_table to default.
	return 0;
}


