#include "page.h"

struct algorithm algo_pbase=
{
	.create = pbase_create,
	.destroy = pbase_destroy,
	.get = pbase_get,
	.set = pbase_set,
	.remove = pbase_remove
};

uint32_t PPA_status = 0 + _PPB;//starts at block 1, not 0.
uint32_t RSV_status = 0;//overprovision area.

int init_done = 0;//check if initial write is done.
TABLE *page_TABLE;
OOB *page_OOB;
SRAM *page_SRAM;
uint16_t *invalid_per_block;

//pthread_mutex_t mutex;//mutex for atomicity in GC logic.
char sync_flag = 0;//end_req mutex flag.
char unload_flag = 0;
uint32_t _g_count = 0;
uint32_t _g_valid = 0;

uint32_t pbase_create(lower_info* li, algorithm *algo) //define & initialize mapping table.
{
	algo->li = li; 	//allocate li parameter to algorithm's li.
	page_TABLE = (TABLE*)malloc(sizeof(TABLE)*_NOP);
	for(int i = 0; i < _NOP; i++)
	{
     	page_TABLE[i].lpa_to_ppa = -1;
		page_TABLE[i].valid_checker = 0;
	}
	
	page_OOB = (OOB *)malloc(sizeof(OOB)*_NOP);
	for (int i = 0; i < _NOP; i++)
	{
		page_OOB[i].reverse_table = -1;
	}	
	page_SRAM = (SRAM*)malloc(sizeof(SRAM)*_PPB);
	for (int i = 0; i<_PPB; i++)
	{
		page_SRAM[i].lpa_RAM = -1;
		page_SRAM[i].VPTR_RAM = NULL;
	}

	invalid_per_block = (uint16_t*)malloc(sizeof(uint16_t)*_NOB);
	for (int i = 0; i<_NOB; i++)
	{
		invalid_per_block[i] = 0;
	}

	printf("pbase_create done!\n");
	return 0;
	//init mapping table.
}	//now we can use page table after pbase_create operation.



void pbase_destroy(lower_info* li, algorithm *algo)
{					  
	free(page_OOB);
	free(invalid_per_block);
	free(page_SRAM);
	free(page_TABLE);
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
	if(sync_flag == 1){//ASYNC GC,
		_g_count++;
		printf("global count : %d\n",_g_count);
	}
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

uint32_t pbase_set(request* const req)
{
	int temp;
	algo_req * my_req;

	bench_algo_start(req);
	my_req = (algo_req*)malloc(sizeof(algo_req));
	my_req->parents = req;
	my_req->end_req = pbase_end_req;

	//garbage_collection necessity detection.
	if (PPA_status == _NOP)
	{
		pbase_garbage_collection();
		init_done = 1;
	}

	else if ((init_done == 1) && (PPA_status % _PPB == 0))
	{
		pbase_garbage_collection();
	}
	//!garbage_collection.
	if (page_TABLE[req->key].lpa_to_ppa != -1)
	{
		temp = page_TABLE[req->key].lpa_to_ppa; //find old ppa.
		page_TABLE[temp].valid_checker = 0; //set that ppa validity to 0.
		invalid_per_block[temp/_PPB] += 1;
	}
	
	page_TABLE[req->key].lpa_to_ppa = PPA_status; //map ppa status to table.
	page_TABLE[PPA_status].valid_checker = 1; 
	page_OOB[PPA_status].reverse_table = req->key;//reverse-mapping.
	bench_algo_end(req);	
	algo_pbase.li->push_data(PPA_status++,PAGESIZE,req->value,ASYNC,my_req);
	return 0;
}

uint32_t pbase_remove(request* const req)
{
	page_TABLE[req->key].lpa_to_ppa = -1; //reset to default.
	page_OOB[req->key].reverse_table = -1; //reset reverse_table to default.
	return 0;
}


