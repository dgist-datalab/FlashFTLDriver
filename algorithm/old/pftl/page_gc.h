#include "page.h"

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
	int target_block = 0;
	int invalid_num = 0;
	int trim_PPA = 0;
	int RAM_PTR = 0;
	value_set **temp_set;
	
	for (int i=0;i<_NOB;i++){
		if(invalid_per_block[i] >= invalid_num){
			target_block = i;
			invalid_num = invalid_per_block[i];
		}
	}//find block with the most invalid block.
	PPA_status = target_block* _PPB;
	trim_PPA = PPA_status; //set trim target.
	_g_valid = _PPB - invalid_num; //set num of valid component. 
	sync_flag = ASYNC;//async mode.

	temp_set = (value_set**)malloc(sizeof(value_set*)*_PPB);
	for (int i=0;i<_PPB;i++){
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
