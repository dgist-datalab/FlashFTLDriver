#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include "../../include/data_struct/lrucache.hpp"
#include "../../bench/bench.h"
#include "page.h"
#include "gc.h"
extern uint32_t test_key;
uint32_t caching_num_lb;
cache::lru_cache <ppa_t, value_set *>* buffer;
align_buffer a_buffer;
extern MeasureTime mt;
struct algorithm demand_ftl={
	.argument_set=demand_argument,
	.create=page_create,
	.destroy=page_destroy,
	.read=page_read,
	.write=page_write,
	.flush=page_flush,
	.remove=NULL,
};

uint32_t page_create (lower_info* li,blockmanager *bm,algorithm *algo){
	algo->li=li; //lower_info means the NAND CHIP
	algo->bm=bm; //blockmanager is managing invalidation 
	buffer=new cache::lru_cache<ppa_t, value_set *>(caching_num_lb);

	demand_map_create(UINT32_MAX,li,bm);

	demand_ftl.algo_body=(void*)pm_body_create(bm);
	a_buffer.value=(char*)malloc(PAGESIZE);
	return 1;
}

void page_destroy (lower_info* li, algorithm *algo){
	demand_map_free();
	free(a_buffer.value);
	return;
}



uint32_t page_read(request *const req){
	if(!req->params){
		value_set *cached_value=buffer->get(req->key);
		if(!cached_value){
			for(uint32_t i=0; i<a_buffer.idx; i++){
				if(req->key==a_buffer.key[i]){
					memcpy(req->value->value, &a_buffer.value[i*4096], 4096);
					req->end_req(req);		
					return 1;
				}
			}
		}
		if(cached_value){
			memcpy(req->value->value, cached_value->value, 4096);
			req->end_req(req);
		}
	}

	return demand_page_read(req);
}

uint32_t align_buffering(request *const req, KEYT key, value_set *value){
	if(req){
		memcpy(&a_buffer.value[a_buffer.idx*4096], req->value->value, 4096);
		a_buffer.key[a_buffer.idx]=req->key;
	}
	else{
		memcpy(&a_buffer.value[a_buffer.idx*4096], req->value->value, 4096);
		a_buffer.key[a_buffer.idx]=key;
	}
	a_buffer.idx++;

	if(a_buffer.idx==L2PGAP){
		ppa_t ppa=get_ppa(a_buffer.key);
		value_set *value=inf_get_valueset(a_buffer.value, FS_MALLOC_W, PAGESIZE);
		send_user_req(NULL, DATAW, ppa, value);
		
		KEYT physical[2];
		physical[0]=ppa*L2PGAP;
		physical[1]=ppa*L2PGAP+1;

		demand_map_assign(req, a_buffer.key, physical);

		a_buffer.idx=0;
		return 1;
	}
	return 0;
}

uint32_t page_write(request *const req){
	if(req->key==2097102){
		printf("2097102 write!!!!\n");
	}
	if(req->params){
		return demand_map_assign(req, NULL, NULL);
	}

	std::pair<ppa_t, value_set *> r; 
	if(caching_num_lb!=0){
		r=buffer->put(req->key, req->value);
		if(r.first!=UINT_MAX){
			if(!align_buffering(NULL, r.first, r.second)){
				req->end_req(req);
			}
		}
	}
	else{
		if(!align_buffering(req, 0, NULL)){
			req->end_req(req);
		}
	}

	return 0;
}

uint32_t page_flush(request *const req){
	abort();
	req->end_req(req);
	return 0;
}

void *page_end_req(algo_req* input){
	//this function is called when the device layer(lower_info) finish the request.
	page_params* params=(page_params*)input->params;
	switch(input->type){
		case DATAW:
			inf_free_valueset(params->value,FS_MALLOC_W);
			break;
		case DATAR:
			if(params->value->ppa%L2PGAP){
				memmove(params->value->value, &params->value->value[4096], 4096);
			}
			break;
	}
	request *res=input->parents;
	if(res){
		res->type_ftl=res->type_lower=0;
		res->end_req(res);//you should call the parents end_req like this
	}
	free(params);
	free(input);
	return NULL;
}
