#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include "../../include/data_struct/lrucache.hpp"
#include "../../interface/vectored_interface.h"
#include "../../bench/bench.h"
#include "page.h"
#include "gc.h"
extern uint32_t test_key;
extern uint32_t debug_lba;
align_buffer a_buffer;
extern MeasureTime mt;
struct algorithm demand_ftl={
	.argument_set=demand_argument,
	.create=page_create,
	.destroy=page_destroy,
	.read=page_read,
	.write=page_write,
	.flush=page_flush,
	.remove=page_remove,
	.test=NULL,
	.print_log=demand_print_log,
	.empty_cache=dftl_empty_cache,
	.dump_prepare=update_cache_mapping,
	.dump=demand_dump,
	.load=demand_load,
};

page_read_buffer rb;
uint32_t read_buffer_hit_cnt=0;


void page_create_body(lower_info *li, blockmanager *bm, algorithm *algo){
	algo->li=li; //lower_info means the NAND CHIP
	algo->bm=bm; //blockmanager is managing invalidation 

	a_buffer.value=(char*)malloc(PAGESIZE);

	rb.pending_req=new std::multimap<uint32_t, algo_req *>();
	rb.issue_req=new std::multimap<uint32_t, algo_req*>();
	fdriver_mutex_init(&rb.pending_lock);
	fdriver_mutex_init(&rb.read_buffer_lock);
	rb.buffer_ppa=UINT32_MAX;

}

uint32_t page_create (lower_info* li,blockmanager *bm,algorithm *algo){
	page_create_body(li, bm, algo);
	demand_map_create(UINT32_MAX,li,bm);
	demand_ftl.algo_body=(void*)pm_body_create(bm);
	return 1;
}

void page_destroy (lower_info* li, algorithm *algo){
	demand_map_free();
	printf("read buffer hit:%u\n", read_buffer_hit_cnt);

	printf("WAF: %lf\n\n",
			(double)(li->req_type_cnt[MAPPINGW] +
				li->req_type_cnt[DATAW]+
				li->req_type_cnt[GCDW]+
				li->req_type_cnt[GCMW_DGC]+
				li->req_type_cnt[GCMW]+
				li->req_type_cnt[COMPACTIONDATAW])/li->req_type_cnt[DATAW]);
	free(a_buffer.value);

	delete rb.pending_req;
	delete rb.issue_req;
	return;
}

uint32_t page_read(request *const req){
	if(!req->param){
		for(uint32_t i=0; i<a_buffer.idx; i++){
			if(req->key==a_buffer.key[i]){
				memcpy(req->value->value, &a_buffer.value[i*LPAGESIZE], LPAGESIZE);
				req->end_req(req);		
				return 1;
			}
		}
	}

	return demand_page_read(req);
}

uint32_t align_buffering(request *const req, KEYT key, value_set *value){

	bool overlap=false;
	uint32_t overlapped_idx=UINT32_MAX;
	for(uint32_t i=0; i<a_buffer.idx; i++){
		if(a_buffer.key[i]==req->key){
			overlapped_idx=i;
			overlap=true;
			break;
		}
	}

	uint32_t target_idx=overlap?overlapped_idx:a_buffer.idx;

	if(req){
		memcpy(&a_buffer.value[target_idx*LPAGESIZE], req->value->value, LPAGESIZE);
		a_buffer.key[target_idx]=req->key;
		a_buffer.prefetching_info[target_idx]=req->consecutive_length;
	}
	else{
		memcpy(&a_buffer.value[target_idx*LPAGESIZE], req->value->value, LPAGESIZE);
		a_buffer.key[target_idx]=key;
		a_buffer.prefetching_info[target_idx]=req->consecutive_length;
	}
	if(req->key==debug_lba){
		printf("%u is buffered\n", debug_lba);
	}

	if(!overlap){ a_buffer.idx++;}

	if(a_buffer.idx==L2PGAP){
		ppa_t ppa=get_ppa(a_buffer.key, a_buffer.idx);
		value_set *value=inf_get_valueset(a_buffer.value, FS_MALLOC_W, PAGESIZE);
#ifdef WRITE_STOP_READ
		send_user_req(req, DATAW, ppa, value);
#else
		send_user_req(NULL, DATAW, ppa, value);
		req->write_done=true;
#endif
		
		KEYT physical[L2PGAP];

		for(uint32_t i=0; i<L2PGAP; i++){
			physical[i]=ppa*L2PGAP+i;	
			if(a_buffer.key[i]==debug_lba){
				printf("%u -> %u[%u] %u \n", debug_lba, physical[i], i ,*(uint32_t*)&a_buffer.value[LPAGESIZE*i]);
			}
		}

		demand_map_assign(req, a_buffer.key, physical, a_buffer.prefetching_info, false);

		a_buffer.idx=0;
		return 1;
	}
	return 0;
}

uint32_t page_write(request *const req){
	if(req->param){
		return demand_map_assign(req, NULL, NULL, NULL, false);
	}
	
	req->write_done=false;
	req->map_done=false;
	fdriver_mutex_init(&req->done_lock);
	/*std::pair<ppa_t, value_set *> r; 
	if(caching_num_lb!=0){
		r=buffer->put(req->key, req->value);
		if(r.first!=UINT_MAX){
			if(!align_buffering(NULL, r.first, r.second)){
				req->end_req(req);
			}
		}
	}
	else{*/
	if(req->key==test_key){
		printf("insert key:%u\n", req->key);
	}
	if(!align_buffering(req, req->key, req->value)){
		req->end_req(req);
	}
	/*}*/

	return 0;
}


uint32_t page_remove(request *const req){
	if(!req->param){
	//	printf("remove start %u\n", req->global_seq);
		for(uint32_t i=0; i<a_buffer.idx; i++){
			if(req->key==a_buffer.key[i]){
				for(uint32_t j=i+1; j<a_buffer.idx; j++){
					if(j+1!=a_buffer.idx){
						a_buffer.key[j]=a_buffer.key[j+1];
						a_buffer.prefetching_info[j]=a_buffer.prefetching_info[j+1];
						memcpy(&a_buffer.value[j*LPAGESIZE], &a_buffer.value[(j+1)*LPAGESIZE], LPAGESIZE);
					}
				}
				a_buffer.idx--;
				req->end_req(req);
				return 1;
			}
		}
	}
	else{
		//printf("remove retry %u\n", req->global_seq);
	}
	return demand_map_assign(req, &req->key, NULL, NULL, true);
}

uint32_t page_flush(request *const req){
	wait_all_request();
	update_cache_mapping();
	wait_all_request_done();
	req->end_req(req);
	return 0;
}

extern struct algorithm demand_ftl;
typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;
void send_user_req(request *const req, uint32_t type, ppa_t ppa,value_set *value){
	/*you can implement your own structur for your specific FTL*/

	if(type==DATAR){
		fdriver_lock(&rb.read_buffer_lock);
		if(ppa==rb.buffer_ppa){
			read_buffer_hit_cnt++;
			memcpy(value->value, &rb.buffer_value[(value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			req->buffer_hit++;
			req->end_req(req);
			fdriver_unlock(&rb.read_buffer_lock);
			return;
		}
		fdriver_unlock(&rb.read_buffer_lock);
	}

	page_params* params=(page_params*)malloc(sizeof(page_params));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	params->value=value;
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->param=(void*)params;//add your parameter structure 
	my_req->type=type;//DATAR means DATA reads, this affect traffics results
	my_req->type_lower=0;
	/*you note that after read a PPA, the callback function called*/


	if(type==DATAR){
		fdriver_lock(&rb.pending_lock);
		rb_r_iter temp_r_iter=rb.issue_req->find(ppa);
		if(temp_r_iter==rb.issue_req->end()){
			rb.issue_req->insert(std::pair<uint32_t,algo_req*>(ppa, my_req));
			fdriver_unlock(&rb.pending_lock);
		}
		else{
			req->buffer_hit++;
			rb.pending_req->insert(std::pair<uint32_t, algo_req*>(ppa, my_req));
			fdriver_unlock(&rb.pending_lock);
			return;
		}
	}

	switch(type){
		case DATAR:
			demand_ftl.li->read(ppa,PAGESIZE,value,my_req);
			break;
		case DATAW:
			demand_ftl.li->write(ppa,PAGESIZE,value,my_req);
			break;
	}
}

static void processing_pending_req(algo_req *req, value_set *v){
	request *parents=req->parents;
	page_params *params=(page_params*)req->param;
	memcpy(params->value->value, &v->value[(params->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
	if(parents){
		if(parents->type_lower < 10){
			parents->type_lower+=req->type_lower;
		}
	}
	parents->end_req(parents);
	free(params);
	free(req);
}

void *page_end_req(algo_req* input){
	//this function is called when the device layer(lower_info) finish the request.
	rb_r_iter target_r_iter;
	algo_req *pending_req;
	page_params* params=(page_params*)input->param;
	request *res=input->parents;
	if(res){
		if(res->type_lower < 10){
			res->type_lower+=input->type_lower;
		}
	}
	switch(input->type){
		case DATAW:
			inf_free_valueset(params->value,FS_MALLOC_W);
			break;
		case DATAR:
		
			fdriver_lock(&rb.pending_lock);
			target_r_iter=rb.pending_req->find(params->value->ppa/L2PGAP);
			for(;target_r_iter->first==params->value->ppa/L2PGAP && 
					target_r_iter!=rb.pending_req->end();){
				pending_req=target_r_iter->second;
				pending_req->type_lower=input->type_lower;
				processing_pending_req(pending_req, params->value);
				rb.pending_req->erase(target_r_iter++);
			}
			rb.issue_req->erase(params->value->ppa/L2PGAP);
			fdriver_unlock(&rb.pending_lock);
			fdriver_lock(&rb.read_buffer_lock);
			rb.buffer_ppa=params->value->ppa/L2PGAP;
			memcpy(rb.buffer_value, params->value->value, PAGESIZE);
			fdriver_unlock(&rb.read_buffer_lock);

			if(params->value->ppa%L2PGAP){
				memmove(params->value->value, &params->value->value[(params->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			}
			break;
	}
	if(input->type==DATAW && res){
		bool should_end_req=false;
		fdriver_lock(&res->done_lock);
		res->write_done=true;
		should_end_req=res->map_done && res->write_done;
		fdriver_unlock(&res->done_lock);
		if(should_end_req){
			fdriver_destroy(&res->done_lock);
			res->end_req(res);
		}
	}
	else if(res){
		res->end_req(res);//you should call the parents end_req like this
	}
	free(params);
	free(input);
	return NULL;
}

extern demand_map_manager dmm;
uint32_t dftl_empty_cache(){
	printf("emtpy cache!!\n");
	wait_all_request();
	update_cache_mapping();
	dmm.cache->empty_cache(dmm.cache);
	wait_all_request_done();
	return 1;
}
