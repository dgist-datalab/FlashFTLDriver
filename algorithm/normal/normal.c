#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include "normal.h"
#include "../../bench/bench.h"
#include "../../interface/interface.h"
struct algorithm __normal={
	.argument_set=NULL,
	.create=normal_create,
	.destroy=normal_destroy,
	.read=normal_read,
	.write=normal_write,
	.remove=NULL,
};

char temp[PAGESIZE];
uint32_t normal_create (lower_info* li,blockmanager *a, algorithm *algo){
	algo->li=li;
	memset(temp,'x',PAGESIZE);
	return 1;
}

void normal_destroy (lower_info* li, algorithm *algo){
	return;
}

uint32_t normal_read(request *const req){
	normal_params* params=(normal_params*)malloc(sizeof(normal_params));
	params->test=-1;

	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=req;
	my_req->end_req=normal_end_req;
	my_req->params=(void*)params;
	my_req->type=DATAR;

	__normal.li->read(req->key,PAGESIZE,req->value,req->isAsync,my_req);
	return 1;
}

uint32_t normal_write(request *const req){
	normal_params* params=(normal_params*)malloc(sizeof(normal_params));
	params->test=-1;

	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=req;
	my_req->end_req=normal_end_req;
	my_req->type=DATAW;
	my_req->params=(void*)params;
	memcpy(req->value->value, &req->key, sizeof(req->key));

	//value_set *temp_value=inf_get_valueset(NULL, PAGESIZE, FS_MALLOC_W);
	__normal.li->write(req->key,PAGESIZE,req->value,req->isAsync,my_req);
	return 0;
}

void *normal_end_req(algo_req* input){
	normal_params* params=(normal_params*)input->params;
	request *res=input->parents;
	uint32_t data;
	switch(input->type){
		case DATAR:
			data=*(uint32_t*)&res->value->value[0];
			printf("lba:%u -> data:%u\n", res->key, data);
			if(data!=res->key){
				printf("LBA and data are differ!\n");
				abort();
			}
			break;
		case DATAW:
			break;
		default:
			printf("undefined type\n");
			abort();
			break;
	}


	res->end_req(res);

	free(params);
	free(input);
	return NULL;
}
