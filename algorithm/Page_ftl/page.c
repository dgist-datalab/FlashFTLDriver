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
	algo->li=li;
	algo->bm=bm;
	page_map_create();
	return 1;
}
void page_destroy (lower_info* li, algorithm *algo){
	page_map_free();
	return;
}

uint32_t page_read(request *const req){
	page_params* params=(page_params*)malloc(sizeof(page_params));
	params->test=-1;

	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=req;
	my_req->end_req=page_end_req;
	my_req->params=(void*)params;
	my_req->type=DATAR;
	my_req->type_lower=0;
	page_ftl.li->read(page_map_pick(req->key),PAGESIZE,req->value,req->isAsync,my_req);
	return 1;
}

uint32_t page_write(request *const req){
	page_params* params=(page_params*)malloc(sizeof(page_params));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=req;
	my_req->end_req=page_end_req;
	my_req->type=DATAW;
	my_req->params=(void*)params;

	memcpy(req->value->value,&req->key,sizeof(req->key));
	page_ftl.li->write(page_map_assign(req->key),PAGESIZE,req->value,req->isAsync,my_req);
	return 0;
}
uint32_t page_remove(request *const req){
	page_ftl.li->trim_block(req->key,NULL);
	return 1;
}
void *page_end_req(algo_req* input){
	page_params* params=(page_params*)input->params;
	request *res=input->parents;
	res->end_req(res);

	free(params);
	free(input);
	return NULL;
}
