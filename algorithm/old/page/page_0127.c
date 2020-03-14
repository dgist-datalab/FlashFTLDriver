#include <stdio.h>
#include <stdlib.h>
#include "page.h"

struct algorithm algo_pbase=
{
	.create = pbase_create,
	.destroy = pbase_destroy,
	.get = pbase_get,
	.set = pbase_set,
	.remove = pbase_remove
};//functions are assigned in this structure.

uint32_t PPA_status = 0;
int init_done = 0;//check if initial write is done.

TABLE *page_TABLE;
OOB *page_OOB;
SRAM *page_SRAM;
uint16_t *invalid_per_block;
//we use these 4 for FTL operation.

uint32_t pbase_create(lower_info* li, algorithm *algo)
{
	algo->li = li;
	page_TABLE = (TABLE*)malloc(sizeof(TABLE)*_NOP);
	//table malloc
	page_OOB = (OOB*)malloc(sizeof(OOB)*_NOP); 
	//virtual OOB. 
	page_SRAM = (SRAM*)malloc(sizeof(SRAM)*256);
	//virtual SRAM.
	invalid_per_block = (uint16_t*)malloc(sizeof(uint16_t)*_NOB);//virtual flag.

	for (int i = 0; i < _NOP; i++)
	{
		page_TABLE[i].lpa_to_ppa = -1;
		page_TABLE[i].valid_checker = 0;
	}//page_TABLE initialization.
	for (int i = 0; i < _NOB; i++)
		invalid_per_block[i] = 0;//invalidity init.
}

void pbase_destroy(lower_info* li, algorithm *algo)
{
	for (int i = 0; i<20; i++)
	{
		printf("ppa:%d, validity:%d\n", 
				page_TABLE[i].lpa_to_ppa, page_TABLE[i].valid_checker);
	}
	
	printf("invalidity: %d\n", invalid_per_block[0]);
	free(page_OOB);
	free(invalid_per_block);
	free(page_SRAM);
	free(page_TABLE);
	//free every memory.
}

void *pbase_end_req(algo_req* input)
{
	pbase_params* params = (pbase_params*)input->params;
	request* res=params->parents;
	res->end_req(res);
	free(params);
	free(input);
	//get input's parameter, find out parent, and free it.
	//afterwards, also free params and input. 
}

void *pbase_algo_end_req(algo_req* input)
{
	free(input);
	//FIXME: since request made in algo does not go up to interface, just freeing input may be enough..
}

uint32_t pbase_get(const request *req)
{
	pbase_params* params = (pbase_params*)malloc(sizeof(pbase_params));
	params->parents=req;
	params->test = -1;
	//default parameter setting.

	algo_req* my_req=(algo_req*)malloc(sizeof(algo_req));
	//init request
	my_req->end_req = pbase_end_req;
	//allocate end_req for my request.
	my_req->params= (void*)params;
	//allocate pointer for param as nulltype.
	
	KEYT target = page_TABLE[req->key].lpa_to_ppa;
	algo_pbase.li->pull_data(target,PAGESIZE,req->value,0,my_req,0);
	//key-value operation
}

uint32_t pbase_set(const request *req)
{
	pbase_params* params = (pbase_params*)malloc(sizeof(pbase_params));
	params->parents = req;
	params->test = -1;
	
	algo_req* my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->end_req = pbase_end_req;
	my_req->params = (void*)params;

	/*garbage collection code would be here*/

	if (page_TABLE[req->key].lpa_to_ppa != -1)
	{
		int temp = page_TABLE[req->key].lpa_to_ppa;
		//find old ppa.
		page_TABLE[temp].valid_checker = 0;
		//set that ppa's validity to 0
		int block_num = temp/_PPB;
		invalid_per_block[block_num] += 1;
	}

	page_TABLE[req->key].lpa_to_ppa = PPA_status;
	page_TABLE[PPA_status].valid_checker = 1;
	page_OOB[PPA_status].reverse_table = req->key;
	KEYT set_target = PPA_status;
	PPA_status++;

	algo_pbase.li->push_data(set_target,PAGESIZE,req->value,0,my_req,0);
}

uint32_t pbase_remove(const request* req)
{
	page_TABLE[req->key].lpa_to_ppa = -1;
	page_OOB[req->key].reverse_table = -1;
}
/*this is clean for non-GC random write.*/
