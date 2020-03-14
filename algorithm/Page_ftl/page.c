#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include "page.h"
#include "map.h"
#include "../../bench/bench.h"


extern MeasureTime mt;
struct algorithm page_ftl={
	.argument_set=NULL,
	.create=page_create,
	.destroy=page_destroy,
	.read=page_read,
	.write=page_write,
	.remove=NULL,
};

uint32_t page_create (lower_info* li,blockmanager *bm,algorithm *algo){
	algo->li=li; //lower_info means the NAND CHIP
	algo->bm=bm; //blockmanager is managing invalidation 
	page_map_create();
	return 1;
}
void page_destroy (lower_info* li, algorithm *algo){
	page_map_free();
	return;
}

uint32_t page_read(request *const req){

	/*you can implement your own structur for your specific FTL*/
	page_params* params=(page_params*)malloc(sizeof(page_params));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->params=(void*)params;//add your parameter structure 
	my_req->type=DATAR;//DATAR means DATA reads, this affect traffics results


	/*you note that after read a PPA, the callback function called*/
	page_ftl.li->read(page_map_pick(req->key),PAGESIZE,req->value,req->isAsync,my_req);
	return 1;
}

uint32_t page_write(request *const req){
	page_params* params=(page_params*)malloc(sizeof(page_params));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=req;
	my_req->end_req=page_end_req;
	my_req->type=DATAW;//DATAW means DATA write, this affect traffics results
	my_req->params=(void*)params;

	memcpy(req->value->value,&req->key,sizeof(req->key));
	page_ftl.li->write(page_map_assign(req->key),PAGESIZE,req->value,req->isAsync,my_req);
	return 0;
}


void *page_end_req(algo_req* input){
	//this function is called when the device layer(lower_info) finish the request.
	page_params* params=(page_params*)input->params;
	request *res=input->parents;
	res->end_req(res);//you should call the parents end_req like this

	free(params);
	free(input);
	return NULL;
}
