#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include "../../include/data_struct/lrucache.hpp"
#include "page.h"
#include "map.h"
#include "../../bench/bench.h"

uint32_t caching_num_lb;
cache::lru_cache <ppa_t, value_set *>* buffer;
align_buffer a_buffer;
extern MeasureTime mt;
struct algorithm page_ftl={
	.argument_set=page_argument,
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
	page_map_create();
	return 1;
}

void page_destroy (lower_info* li, algorithm *algo){
	page_map_free();
	return;
}

inline void send_user_req(request *const req, uint32_t type, ppa_t ppa,value_set *value){
	/*you can implement your own structur for your specific FTL*/
	page_params* params=(page_params*)malloc(sizeof(page_params));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	params->value=value;
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->params=(void*)params;//add your parameter structure 
	my_req->type=type;//DATAR means DATA reads, this affect traffics results
	/*you note that after read a PPA, the callback function called*/


	switch(type){
		case DATAR:
			params->address=ppa%L2PGAP;
			page_ftl.li->read(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
		case DATAW:
			page_ftl.li->write(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
	}
}

uint32_t page_read(request *const req){
	
	value_set *cached_value=buffer->get(req->key);
	if(!cached_value){
		for(uint32_t i=0; i<a_buffer.idx; i++){
			if(req->key==a_buffer.key[i]){
				memcpy(req->value->value, a_buffer.value[i]->value, 4096);
				req->end_req(req);		
				return 1;
			}
		}
	}

	if(cached_value){
		memcpy(req->value->value, cached_value->value, 4096);
		req->end_req(req);
	}
	else{
		send_user_req(req, DATAR, page_map_pick(req->key)/L2PGAP, req->value);
	}
	return 1;
}

uint32_t align_buffering(request *const req, KEYT key, value_set *value){
	if(req){
		a_buffer.value[a_buffer.idx]=req->value;
		a_buffer.key[a_buffer.idx]=req->key;
	}
	else{
		a_buffer.value[a_buffer.idx]=value;
		a_buffer.key[a_buffer.idx]=key;
	}
	a_buffer.idx++;

	if(a_buffer.idx==L2PGAP){
		ppa_t ppa=page_map_assign(a_buffer.key);
		value_set *value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		for(uint32_t i=0; i<L2PGAP; i++){
			memcpy(&value->value[i*4096], a_buffer.value[i]->value, 4096);
			inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
		}
		send_user_req(NULL, DATAW, ppa, value);
		a_buffer.idx=0;
	}
	return 1;
}

uint32_t page_write(request *const req){
	std::pair<ppa_t, value_set *> r; 
	if(caching_num_lb!=0){
		r=buffer->put(req->key, req->value);
		if(r.first!=UINT_MAX){
			align_buffering(NULL, r.first, r.second);
			//send_user_req(NULL, DATAW, page_map_assign(r.first), r.second);
		}
		req->value=NULL;
		req->end_req(req);
	}
	else{
		align_buffering(req, 0, NULL);
		req->value=NULL;
		req->end_req(req);
		//send_user_req(req, DATAW, page_map_assign(req->key), req->value);
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
	}
	request *res=input->parents;
	if(res){
		res->end_req(res);//you should call the parents end_req like this
	}
	free(params);
	free(input);
	return NULL;
}

inline uint32_t xx_to_byte(char *a){
	switch(a[0]){
		case 'K':
			return 1024;
		case 'M':
			return 1024*1024;
		case 'G':
			return 1024*1024*1024;
		default:
			break;
	}
	return 1;
}

uint32_t page_argument(int argc, char **argv){
	bool cache_size;
	uint32_t len;
	int c;
	char temp;
	uint32_t base;
	uint32_t value;
	while((c=getopt(argc,argv,"c"))!=-1){
		switch(c){
			case 'c':
				cache_size=true;
				len=strlen(argv[optind]);
				temp=argv[optind][len-1];
				if(temp < '0' || temp >'9'){
					argv[optind][len-1]=0;
					base=xx_to_byte(&temp);
				}
				value=atoi(argv[optind]);
				value*=base;
				caching_num_lb=value/4096;
				break;
			default:
				break;
		}
	}
	return 1;
}
