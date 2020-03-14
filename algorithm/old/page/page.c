#include <string.h>
#include <stdlib.h>
#include "page.h"

struct algorithm algo_pbase=
{
	.create = pbase_create,
	.destroy = pbase_destroy,
	.get = pbase_get,
	.set = pbase_set,
	.remove = pbase_remove
};
//this is for interface: users use this structure to call functions!

int32_t *lpa_to_ppa; //pointer for page table.
uint32_t PPA_status = 0;//initalize physical page address.
int32_t NUMOP = 32*1024*1024;
int32_t NUM = 32;
uint32_t pbase_create(lower_info* li, algorithm *algo) //define & initialize mapping table.
{
	algo->li = li; //allocate li parameter to algorithm's li.
	
	lpa_to_ppa = (int32_t*)malloc(sizeof(int32_t)*NUMOP);
	for(int i = 0; i < NUMOP; i++)
		lpa_to_ppa[i] = -1; 
	//init mapping table.
}//now we can use page table after pbase_create operation.

void pbase_destroy(lower_info* li, algorithm *algo)
{
	free(lpa_to_ppa);//deallocate table.
	//Question: why normal_destroy need li and algo?
}

void *pbase_end_req(algo_req* input)
{
	pbase_params* params=(pbase_params*)input->params;
	request *res=params->parents;
	res->end_req(res);
	free(params);
	free(input);
}

uint32_t pbase_get(const request *req)
{
	//put request in normal_param first.
	//request has a type, key and value.
	pbase_params* params = (pbase_params*)malloc(sizeof(pbase_params));
	params->parents=req;
	params->test=-1; //default parameter setting.
	
	
	algo_req * my_req = (algo_req*)malloc(sizeof(algo_req)); //init reqeust
	my_req->end_req=pbase_end_req;//allocate end_req for request.
	my_req->params=(void*)params;//allocate parameter for request.	
	
	KEYT target = lpa_to_ppa[req->key];
	
	algo_pbase.li->pull_data(target,PAGESIZE,req->value,0,my_req,0);
	//key-value operation. 
	//Question: why value type is char*? 
	
}

uint32_t pbase_set(const request *req)
{
	pbase_params* params = (pbase_params*)malloc(sizeof(pbase_params));
	params->parents=req;
	params->test=-1;
	
	algo_req * my_req = (algo_req*)malloc(sizeof(algo_req));
	my_req->end_req = pbase_end_req;
	my_req->params = (void*)params;
	
	lpa_to_ppa[req->key] = PPA_status; //map ppa status to table.
	KEYT set_target = PPA_status;
	PPA_status++;
	
	algo_pbase.li->push_data(set_target,PAGESIZE,req->value,0,my_req,0);
}

uint32_t pbase_remove(const request *req)
{
	lpa_to_ppa[req->key] = -1; //reset to default.
}
