#include "page.h"

struct algorithm algo_pbase=
{
	.create = pbase_create,
	.destroy = pbase_destroy,
	.get = pbase_get,
	.set = pbase_set,
	.remove = pbase_remove
};

uint64_t PPA_status = 0 + _PPS;//starts at block 1, not 0.
uint64_t RSV_status = 0;//overprovision area.

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
value_set* _g_unload_value;

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
	page_SRAM = (SRAM*)malloc(sizeof(SRAM)*_PPS);
	for (int i = 0; i<_PPS; i++)
	{
		page_SRAM[i].lpa_RAM = -1;
		page_SRAM[i].VPTR_RAM = NULL;
	}

	invalid_per_block = (uint16_t*)malloc(sizeof(uint16_t)*_NOS);
	for (int i = 0; i<_NOS; i++)
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
	
	bench_algo_start(req);
	algo_req * my_req = (algo_req*)malloc(sizeof(algo_req)); //init reqeust
	my_req->parents = req;
	my_req->end_req=pbase_end_req;//allocate end_req for request.
	int target = page_TABLE[req->key].lpa_to_ppa;
	bench_algo_end(req);	
	if (target == -1){
		pbase_end_req(my_req);
		return 0;
	}
	else{
		algo_pbase.li->pull_data(target,PAGESIZE,req->value,ASYNC,my_req);
/*		printf("\n==== get data ===\n");
		printf("target is : %d\n",req->key);
		printf("assigned ppa is %d\n",target);
		printf("==== get done ===\n");
*/
	}
	//key-value operation.
	return 0;
}

uint32_t pbase_set(request* const req)
{

	bench_algo_start(req);
	algo_req * my_req = (algo_req*)malloc(sizeof(algo_req));
	my_req->parents = req;
	my_req->end_req = pbase_end_req;

	//garbage_collection necessity detection.
	if (PPA_status == _NOP)
	{
		pbase_garbage_collection();
		init_done = 1;
	}

	else if ((init_done == 1) && (PPA_status % _PPS == 0))
	{
		pbase_garbage_collection();
	}
	//!garbage_collection.
	if (page_TABLE[req->key].lpa_to_ppa != -1)
	{
		printf("update [%d]lpa. mapped ppa was [%d]\n",req->key,page_TABLE[req->key].lpa_to_ppa); 
		int64_t temp = page_TABLE[req->key].lpa_to_ppa; //find old ppa.
		page_TABLE[temp].valid_checker = 0; //set that ppa validity to 0.
		int64_t b_temp = temp / _PPS;
		invalid_per_block[b_temp] += 1;
		printf("old ppa: %d\n",temp);
		printf("invalidated [%d]th block :%d\n",b_temp,invalid_per_block[b_temp]);
	}
	
	page_TABLE[req->key].lpa_to_ppa = PPA_status; //map ppa status to table.
	printf("req-> key:%d, mapped ppa: %d\n",req->key, PPA_status);
	page_TABLE[PPA_status].valid_checker = 1; 
	page_OOB[PPA_status].reverse_table = req->key;//reverse-mapping.
	KEYT set_target = PPA_status;
	PPA_status++;
	bench_algo_end(req);	
	
/*	printf("\n==== set data ===\n");
	printf("target is : %d\n",req->key);
	printf("value is : %c\n",req->value->value[0]);
	printf("==== set done ===\n"); */
/*	if (PPA_status % _PPS == 0){
		printf("start loadcheck.\n");
		for (int i=0;i<_PPS;i++){
			value_set* value_PTR ; //make new value_set
			value_PTR = inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			algo_req * inf_test = (algo_req*)malloc(sizeof(algo_req));
			inf_test->parents = NULL;
			inf_test->end_req = pbase_algo_end_req; //request termination.
			int ppa = PPA_status - _PPS + i;
			algo_pbase.li->pull_data(ppa,PAGESIZE,value_PTR,0,inf_test);
			printf("\n=====LOADCHECK=====\n");
			printf("target ppa : %d\n",ppa);
			printf("loaded item : %d\n",value_PTR->value[0]);
			printf("===LOADCHECK_END===\n");
		}
	}
*/
/*
	if (PPA_status % _PPS == 0){
		printf("start trim test. remove written data.\n");
		int targ = (PPA_status-1)/_PPS;
		printf("targ %d\n",targ);
		algo_pbase.li->trim_block(targ,false);
		for (int i = 0; i< _PPS;i++)
		{
			value_set* value_PTR ; //make new value_set
			value_PTR = inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			algo_req * inf_test = (algo_req*)malloc(sizeof(algo_req));
			inf_test->parents = NULL;
			inf_test->end_req = pbase_algo_end_req; //request termination.
			int ppa = PPA_status - _PPS + i;
			algo_pbase.li->pull_data(ppa,PAGESIZE,value_PTR,0,inf_test);
			printf("\n=====TRIMCHECK=====\n");
			printf("target ppa : %d\n",ppa);
			printf("loaded item : %d\n",value_PTR->value[0]);
			printf("===TRIMCHECK_END===\n");
		}
	}
*/
	algo_pbase.li->push_data(set_target,PAGESIZE,req->value,ASYNC,my_req);
	return 0;
}

uint32_t pbase_remove(request* const req)
{
	page_TABLE[req->key].lpa_to_ppa = -1; //reset to default.
	page_OOB[req->key].reverse_table = -1; //reset reverse_table to default.
	return 0;
}

value_set* SRAM_load(int ppa, int a)
{
	value_set* value_PTR ; //make new value_set
	value_PTR = inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
	algo_req * my_req = (algo_req*)malloc(sizeof(algo_req));
	my_req->parents = NULL;
	my_req->end_req = pbase_algo_end_req; //make pseudo reqeust.
	algo_pbase.li->pull_data(ppa,PAGESIZE,value_PTR,ASYNC,my_req);//end_req will free input and increases count.
	page_SRAM[a].lpa_RAM = page_OOB[ppa].reverse_table;//load reverse-mapped lpa.
	page_SRAM[a].VPTR_RAM = (value_set*)malloc(sizeof(value_set));
	return value_PTR;
}

value_set* SRAM_unload(int ppa, int a)
{
	value_set *value_PTR;
	value_PTR = inf_get_valueset(page_SRAM[a].VPTR_RAM->value,FS_MALLOC_W,PAGESIZE);//set valueset as write mode.
	algo_req * my_req = (algo_req*)malloc(sizeof(algo_req));
	my_req->end_req = pbase_algo_end_req;
	my_req->parents = NULL;
	algo_pbase.li->push_data(ppa,PAGESIZE,value_PTR,ASYNC,my_req);
	page_TABLE[page_SRAM[a].lpa_RAM].lpa_to_ppa = ppa;
	page_TABLE[ppa].valid_checker = 1;
	page_OOB[ppa].reverse_table = page_SRAM[a].lpa_RAM;
	page_SRAM[a].lpa_RAM = -1;
	free(page_SRAM[a].VPTR_RAM);
	return value_PTR;
}

uint32_t pbase_garbage_collection()//do pbase_read and pbase_set 
{
	printf("\nenterged GC..!\n");
	int target_block = 0;
	int invalid_num = 0;
	int trim_PPA = 0;
	int RAM_PTR = 0;
	value_set **temp_set;
	
	for (int i=0;i<_NOS;i++){
		if(invalid_per_block[i] >= invalid_num){
			target_block = i;
			invalid_num = invalid_per_block[i];
		}
	}//find block with the most invalid block.
	printf("target block is %d, valid num is %d.\n",target_block,_PPS - invalid_num);
	printf("_PPS: %d, invalid_num = %d\n",_PPB, invalid_num);
	PPA_status = target_block* _PPS;
	trim_PPA = PPA_status; //set trim target.
	_g_valid = _PPS - invalid_num; //set num of valid component. 
	sync_flag = ASYNC;//async mode.

	temp_set = (value_set**)malloc(sizeof(value_set*)*_PPS);
	for (int i=0;i<_PPS;i++){
		if (page_TABLE[trim_PPA + i].valid_checker == 1){
			temp_set[RAM_PTR] = SRAM_load(trim_PPA + i, RAM_PTR);
			RAM_PTR++;
			page_TABLE[trim_PPA + i].valid_checker = 0;
		}
	}
	while(_g_count != _g_valid){}//wait until count reaches valid.

	_g_count = 0;
	
	for (int i=0;i<_g_valid;i++){  //if read is finished, copy value_set and free original one.
		memcpy(page_SRAM[i].VPTR_RAM,temp_set[i],sizeof(value_set));
		inf_free_valueset(temp_set[i],FS_MALLOC_R);
	}
	
	for (int i=0;i<_g_valid;i++){
		temp_set[i] = SRAM_unload(RSV_status,i);
		RSV_status++;
	}//unload.
	
	while(_g_count != _g_valid){}//wait until count reaches valid.

	for (int i=0;i<_g_valid;i++)
		inf_free_valueset(temp_set[i],FS_MALLOC_W);

	_g_count = 0;
	sync_flag = 0;//reset sync flag.


	int temp = PPA_status;
	PPA_status = RSV_status;
	RSV_status = temp;//swap Reserved area.
	
	invalid_per_block[target_block] = 0;
	algo_pbase.li->trim_block(trim_PPA, false);
	return 0;
}
