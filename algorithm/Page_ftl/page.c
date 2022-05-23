#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../include/data_struct/lrucache.hpp"
#include "../../include/debug_utils.h"
#include "page.h"
#include "map.h"
#include "../../bench/bench.h"
#include "./rmw_checker.h"

//#define LBA_LOGGING "/lba_log"

#ifdef LBA_LOGGING
uint32_t log_fd;
#endif


extern uint32_t test_key;
align_buffer a_buffer;
typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;
page_read_buffer rb;

extern MeasureTime mt;
uint32_t print_traffic();

struct algorithm page_ftl={
	.argument_set=page_argument,
	.create=page_create,
	.destroy=page_destroy,
	.read=page_read,
	.write=page_write,
	.flush=page_flush,
	.remove=page_remove,
	.rmw=page_rmw,
	.test=NULL,
	.print_log=print_traffic,
	.dump_prepare=NULL,
	.dump=page_dump,
	.load=page_load,
};

uint32_t print_traffic(){
	return 1;
}

void page_create_body(lower_info *li, blockmanager *bm, algorithm *algo){
	algo->li=li; //lower_info means the NAND CHIP
	algo->bm=bm; //blockmanager is managing invalidation 

	rb.pending_req=new std::multimap<uint32_t, algo_req *>();
	rb.issue_req=new std::multimap<uint32_t, algo_req*>();
	fdriver_mutex_init(&rb.pending_lock);
	fdriver_mutex_init(&rb.read_buffer_lock);
	rb.buffer_ppa=UINT32_MAX;
	rmw_node_init();

#ifdef LBA_LOGGING
	log_fd=open(LBA_LOGGING, 0x666, O_CREAT | O_WRONLY | O_TRUNC);
	if (log_fd < 0) {
		perror("Failed to open " LBA_LOGGING);
		abort();
		return 1;
	}
#endif
}

uint32_t page_create (lower_info* li,blockmanager *bm,algorithm *algo){
	page_create_body(li, bm, algo);
	page_map_create();
	return 1;
}

void page_destroy (lower_info* li, algorithm *algo){
#ifdef LBA_LOGGING
	close(log_fd);
#endif
	//page_map_free();
	
	delete rb.pending_req;
	delete rb.issue_req;
	return;
}

inline void send_user_req(request *const req, uint32_t type, ppa_t ppa,value_set *value){
	/*you can implement your own structur for your specific FTL*/

#ifdef RMW
#else
	if(type==DATAR){
		fdriver_lock(&rb.read_buffer_lock);
		if(ppa==rb.buffer_ppa){
			if(test_key==req->key){
				printf("%u page hit(piece_ppa:%u)\n", req->key,value->ppa);
			}
			if(L2PGAP==1){
				if(req->offset!=0){
					memmove(value->value, &rb.buffer_value[req->offset*LPAGESIZE], req->length*LPAGESIZE);
				}
			}
			else{
				memcpy(value->value, &rb.buffer_value[(value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			}
			req->buffer_hit++;
			req->end_req(req);
			fdriver_unlock(&rb.read_buffer_lock);
			return;
		}
		fdriver_unlock(&rb.read_buffer_lock);
	}
#endif

	page_param* param=(page_param*)malloc(sizeof(page_param));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	param->value=value;
	param->rmw_value=NULL;
	param->rmw_bitmap=0;
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->param=(void*)param;//add your parameter structure 
	my_req->type=type;//DATAR means DATA reads, this affect traffics results
	my_req->type_lower=0;
	/*you note that after read a PPA, the callback function called*/

#ifdef RMW
#else
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
#endif


#ifdef RMW
	if(type==DATAR && rmw_check(req->key)){
		param->rmw_value=NULL;
		//param->rmw_value=(char*)malloc(PAGESIZE);
		//param->rmw_bitmap=rmw_node_pick(req->key, req->global_seq, param->rmw_value);
		//printf("partial read %u ppa:%u\n", req->key, ppa);
	}
	else{
		param->rmw_value=NULL;
	}

#endif

	switch(type){
		case DATAR:
			page_ftl.li->read(ppa,PAGESIZE,value,my_req);
			break;
		case DATAW:
			page_ftl.li->write(ppa,PAGESIZE,value,my_req);
			break;
	}
}

bool testing;
uint32_t testing_lba;

uint32_t page_read(request *const req){
#ifdef LBA_LOGGING
	dprintf(log_fd, "R %u\n",req->key);
#endif
	bool debug=false;
	for(uint32_t i=0; i<req->length; i++){
		uint32_t LBA=req->key*R2LGAP+req->offset+i;
		if(LBA==test_key){
			printf("%u read!!\n", LBA);
			debug=true;
		}
	}
	if(!testing){
		testing_lba=req->key;
		testing=true;
	}

	if(req->key==0  && req->offset==0 && req->length==1){
		//GDB_MAKE_BREAKPOINT;
	}

//	printf("issue %u %u\n", req->seq, req->key);
	for(uint32_t i=0; i<a_buffer.idx; i++){
		if(req->key==a_buffer.key[i]){
			//		printf("buffered read!\n");
			memcpy(req->value->value, a_buffer.value[i]->value, LPAGESIZE);
			req->end_req(req);		
			return 1;
		}
	}

	//printf("read key :%u\n",req->key);

	req->value->ppa=page_map_pick(req->key);
	if(debug){
		printf("test_key(%u) read %u\n", test_key, req->value->ppa);
	}

	//DPRINTF("\t\tmap info : %u->%u\n", req->key, req->value->ppa);
	if(req->value->ppa==UINT32_MAX){
		if(!rmw_node_merge(req->key, req->global_seq, req->value->value)){
			req->type=FS_NOTFOUND_T;
		}
		req->end_req(req);
	}
	else{
		send_user_req(req, DATAR, req->value->ppa/L2PGAP, req->value);
	}
	return 1;
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
		if(overlap){
			inf_free_valueset(a_buffer.value[target_idx], FS_MALLOC_W);
		}
		a_buffer.value[target_idx]=req->value;
		a_buffer.key[target_idx]=req->key;
	}
	else{
		if(overlap){
			inf_free_valueset(a_buffer.value[target_idx], FS_MALLOC_W);
		}
		a_buffer.value[target_idx]=value;
		a_buffer.key[target_idx]=key;
	}

	if(!overlap){ a_buffer.idx++;}

	if(a_buffer.idx==L2PGAP){
		ppa_t ppa=page_map_assign(a_buffer.key, a_buffer.idx);
		value_set *value;
		if(L2PGAP==1){
			value=req->value;
		}
		else{
			value=inf_get_valueset(NULL,  FS_MALLOC_W, PAGESIZE);
			for(uint32_t i=0; i<L2PGAP; i++){
				if(a_buffer.key[i]==0){
					printf("target key:%u->%u\n", 0,ppa*L2PGAP+i);
				}
				memcpy(&value->value[i*LPAGESIZE], a_buffer.value[i]->value, LPAGESIZE);
				inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
			}
		}

		if(req->key==test_key/4){
			uint32_t value;
			for(uint32_t i=0; i<4; i++){
				uint32_t offset=test_key%4;
				value=*(uint32_t*)&req->value->value[offset*LPAGESIZE];
			}
			printf("test_key(%u) is set to %u, crc:%u seq:%u\n", test_key, ppa,value, req->global_seq);
		}
		req->value=NULL;
#ifdef WRITE_STOP_READ
		send_user_req(req, DATAW, ppa, value);
#else
		send_user_req(NULL, DATAW, ppa, value);
#endif
		a_buffer.idx=0;
		return 0;
	}
	return 1;
}

uint32_t page_write(request *const req){
#ifdef LBA_LOGGING
	dprintf(log_fd, "W %u\n",req->key);
#endif
	if(req->key==0 && req->offset==0 && req->length==1){
		//GDB_MAKE_BREAKPOINT;
	}

	if(align_buffering(req, req->key, req->value)!=0){
#ifdef WRITE_STOP_READ
		req->value=NULL;
		req->end_req(req);
#endif
	}

#ifdef WRITE_STOP_READ
#else
	req->value=NULL;
	req->end_req(req);
#endif
	return 0;
}

uint32_t page_remove(request *const req){
	if (L2PGAP>1)
	{
		for (uint8_t i = 0; i < a_buffer.idx; i++)
		{
			if (a_buffer.key[i] == req->key)
			{
				inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
				if (i == 1)
				{
					a_buffer.value[0] = a_buffer.value[1];
					a_buffer.key[0] = a_buffer.key[1];
				}

				a_buffer.idx--;
				goto end;
			}
		}

		page_map_trim(req->key);
	}
	end:
	req->end_req(req);
	return 0;
}

void *rmw_end_req(algo_req *input){
	request *preq=input->parents;
	page_param *param=(page_param*)input->param;
	//preq->type=FS_SET_T;

	//memcpy(&param->value->value[preq->offset*LPAGESIZE], preq->value->value, preq->length*LPAGESIZE);
	//inf_free_valueset(preq->value, FS_MALLOC_W);
	//preq->value=param->value;

	if(param->rmw_value){
		for(uint32_t i=0; i<R2LGAP; i++){
			if(preq->offset<=i && i<preq->offset+preq->length) continue;
			if((1<<i) && param->rmw_bitmap){
				memcpy(&preq->value->value[i*LPAGESIZE], &param->rmw_value[i*LPAGESIZE], LPAGESIZE);
			}
		}
		free(param->rmw_value);
	}

	if(preq->offset!=0){
		memcpy(preq->value->value, param->value->value, preq->offset*LPAGESIZE);
	}

	if(preq->offset+preq->length!=R2LGAP){
		memcpy(&preq->value->value[(preq->offset+preq->length)*LPAGESIZE], &param->value->value[(preq->offset+preq->length)*LPAGESIZE], (R2LGAP-preq->offset-preq->length)*LPAGESIZE);
	}

	

	inf_free_valueset(param->value,FS_MALLOC_R);
	rmw_node_read_done(preq->key, preq->global_seq);

	//fdriver_unlock(param->rmw_lock);

	free(param);
	free(input);

	
	preq->rmw_state=RMW_READ_DONE;
	if(!inf_assign_try(preq)){
		abort();
	}

	return NULL;
}
uint32_t page_rmw(request *const req){
	uint32_t ppa=page_map_pick(req->key);

	for(uint32_t i=0; i<req->length; i++){
		uint32_t LBA=req->key*R2LGAP+req->offset+i;
		if(LBA==test_key){
			printf("%u rmw data:%u\n", LBA, *(uint32_t*)&req->value->value[(req->offset+i)*LPAGESIZE]);
		}
	}/*
	if(req->key==test_key/4){
		printf("rmw target key set seq_id:%u\n",req->global_seq);
	}*/

	if(ppa==UINT32_MAX){
		rmw_node_insert(req->key, req->offset, req->length, req->global_seq, req->value->value);
		rmw_node_read_done(req->key, req->global_seq);
		req->rmw_state=RMW_READ_DONE;
		return page_write(req);
		/*
		if(!inf_assign_try(req)){
			abort();
		}*/
	}
	else if(req->rmw_state==RMW_START){
		page_param *param=(page_param*)malloc(sizeof(page_param));
		if(rmw_check(req->key)){
			//printf("rmw checked! %u\n", req->global_seq);
			param->rmw_value=(char*)malloc(PAGESIZE);
			param->rmw_bitmap=rmw_node_pick(req->key, req->global_seq, param->rmw_value);
		}
		else{
			param->rmw_value=NULL;
		}
		algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
		param->value=inf_get_valueset(NULL,FS_MALLOC_R, PAGESIZE);
		my_req->parents=req;
		my_req->end_req=rmw_end_req;
		my_req->param=(void*)param;
		my_req->type=DATAR;
		my_req->type_lower=0;

		rmw_node_insert(req->key, req->offset, req->length, req->global_seq, req->value->value);

		page_ftl.li->read(ppa, PAGESIZE, param->value, my_req);
	}
	else{
		return page_write(req);
	}
	return 1;
}

uint32_t page_flush(request *const req){
	abort();
	req->end_req(req);
	return 0;
}
static void processing_pending_req(algo_req *req, value_set *v){
	request *parents=req->parents;
	page_param *param=(page_param*)req->param;
	if(L2PGAP==1){
		if(parents->offset!=0){
			memmove(parents->value->value, &v->value[parents->offset*LPAGESIZE], parents->length*LPAGESIZE);
		}
	}
	else{
		memcpy(parents->value->value, &v->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
	}
	if(parents){
		if(parents->type_lower < 10){
			parents->type_lower+=req->type_lower;
		}
	}
	parents->end_req(parents);
	free(param);
	free(req);
}

void *page_end_req(algo_req* input){
	//this function is called when the device layer(lower_info) finish the request.
	rb_r_iter target_r_iter;
	rb_r_iter target_r_iter_temp;
	algo_req *pending_req;
	page_param* param=(page_param*)input->param;
	request *res=input->parents;
	if(res){
		if(res->type_lower < 10){
			res->type_lower+=input->type_lower;
		}
	}
	uint32_t i, LBA;
	switch(input->type){
		case DATAW:
			inf_free_valueset(param->value,FS_MALLOC_W);
			if(res->offset!=0 || res->length!=R2LGAP){
				rmw_node_delete(res->key, res->global_seq);
			}
			if(res->type==FS_RMW_T){
				fdriver_unlock(res->rmw_lock);
			}
			break;
		case DATAR:
			#ifdef RMW
			if(param->rmw_value){
				for(i=0; i<R2LGAP; i++){
					if((1<<i) && param->rmw_bitmap){
						memcpy(&res->value->value[i*LPAGESIZE], &param->rmw_value[i*LPAGESIZE], LPAGESIZE);
					}
				}
				free(param->rmw_value);
			}
			#else
			fdriver_lock(&rb.pending_lock);
			target_r_iter=rb.pending_req->find(param->value->ppa/L2PGAP);
			for(;target_r_iter->first==param->value->ppa/L2PGAP && 
					target_r_iter!=rb.pending_req->end();){
				pending_req=target_r_iter->second;
				pending_req->type_lower=input->type_lower;
				processing_pending_req(pending_req, param->value);
				rb.pending_req->erase(target_r_iter++);
			}
			rb.issue_req->erase(param->value->ppa/L2PGAP);
			fdriver_unlock(&rb.pending_lock);

			fdriver_lock(&rb.read_buffer_lock);
			rb.buffer_ppa=param->value->ppa/L2PGAP;
			memcpy(rb.buffer_value, param->value->value, PAGESIZE);
			fdriver_unlock(&rb.read_buffer_lock);
			#endif
			if(L2PGAP==1){
				if(res->offset!=0){
					memmove(param->value->value, &param->value->value[res->offset*LPAGESIZE], res->length*LPAGESIZE);
				}
			}
			else{
				if(param->value->ppa%L2PGAP){
					memmove(param->value->value, &param->value->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
				}
			}
			break;
	}
	res->end_req(res);//you should call the parents end_req like this
	free(param);
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
				break;
			default:
				break;
		}
	}
	return 1;
}
