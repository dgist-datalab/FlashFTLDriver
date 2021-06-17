#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../include/data_struct/lrucache.hpp"
#include "page.h"
#include "map.h"
#include "../../bench/bench.h"

//#define LBA_LOGGING "/lba_log"

#ifdef LBA_LOGGING
uint32_t log_fd;
#endif


extern uint32_t test_key;
align_buffer a_buffer;
typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;

extern MeasureTime mt;
struct algorithm page_ftl={
	.argument_set=page_argument,
	.create=page_create,
	.destroy=page_destroy,
	.read=page_read,
	.write=page_write,
	.flush=page_flush,
	.remove=page_remove,
};

page_read_buffer rb;

uint32_t page_create (lower_info* li,blockmanager *bm,algorithm *algo){
	algo->li=li; //lower_info means the NAND CHIP
	algo->bm=bm; //blockmanager is managing invalidation 
	page_map_create();

	rb.pending_req=new std::multimap<uint32_t, algo_req *>();
	rb.issue_req=new std::multimap<uint32_t, algo_req*>();
	fdriver_mutex_init(&rb.pending_lock);
	fdriver_mutex_init(&rb.read_buffer_lock);
	rb.buffer_ppa=UINT32_MAX;

#ifdef LBA_LOGGING
	log_fd=open(LBA_LOGGING, 0x666, O_CREAT | O_WRONLY | O_TRUNC);
	if (log_fd < 0) {
		perror("Failed to open " LBA_LOGGING);
		abort();
		return 1;
	}
#endif
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
	if(type==DATAR){
		fdriver_lock(&rb.read_buffer_lock);
		if(ppa==rb.buffer_ppa){
			if(test_key==req->key){
				printf("%u page hit(piece_ppa:%u)\n", req->key,value->ppa);
			}
			memcpy(value->value, &rb.buffer_value[(value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			req->type_ftl=req->type_lower=0;
			req->end_req(req);
			fdriver_unlock(&rb.read_buffer_lock);
			return;
		}
		fdriver_unlock(&rb.read_buffer_lock);
	}

	page_param* param=(page_param*)malloc(sizeof(page_param));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	param->value=value;
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->param=(void*)param;//add your parameter structure 
	my_req->type=type;//DATAR means DATA reads, this affect traffics results
	/*you note that after read a PPA, the callback function called*/


	if(type==DATAR){
		fdriver_lock(&rb.pending_lock);
		rb_r_iter temp_r_iter=rb.issue_req->find(ppa);
		if(temp_r_iter==rb.issue_req->end()){
			rb.issue_req->insert(std::pair<uint32_t,algo_req*>(ppa, my_req));
			fdriver_unlock(&rb.pending_lock);
		}
		else{
			rb.pending_req->insert(std::pair<uint32_t, algo_req*>(ppa, my_req));
			fdriver_unlock(&rb.pending_lock);
			return;
		}
	}

	switch(type){
		case DATAR:
			page_ftl.li->read(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
		case DATAW:
			page_ftl.li->write(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
	}
}

bool testing;
uint32_t testing_lba;

uint32_t page_read(request *const req){
#ifdef LBA_LOGGING
	dprintf(log_fd, "R %u\n",req->key);
#endif

	if(!testing){
		testing_lba=req->key;
		testing=true;
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

	//DPRINTF("\t\tmap info : %u->%u\n", req->key, req->value->ppa);
	if(req->value->ppa==UINT32_MAX){
		req->type=FS_NOTFOUND_T;
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
		a_buffer.value[target_idx]=req->value;
		a_buffer.key[target_idx]=req->key;
	}
	else{
		a_buffer.value[target_idx]=value;
		a_buffer.key[target_idx]=key;
	}

	if(!overlap){ a_buffer.idx++;}

	if(a_buffer.idx==L2PGAP){
		ppa_t ppa=page_map_assign(a_buffer.key, a_buffer.idx);
		value_set *value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		for(uint32_t i=0; i<L2PGAP; i++){
			memcpy(&value->value[i*LPAGESIZE], a_buffer.value[i]->value, LPAGESIZE);
			inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
		}
		send_user_req(NULL, DATAW, ppa, value);
		a_buffer.idx=0;
	}
	return 1;
}

uint32_t page_write(request *const req){
#ifdef LBA_LOGGING
	dprintf(log_fd, "W %u\n",req->key);
#endif
	//printf("write key :%u\n",req->key);
	align_buffering(req, 0, NULL);
	req->value=NULL;
	req->end_req(req);
	//send_user_req(req, DATAW, page_map_assign(req->key), req->value);
	return 0;
}

uint32_t page_remove(request *const req){
	for(uint8_t i=0; i<a_buffer.idx; i++){
		if(a_buffer.key[i]==req->key){
			inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
			if(i==1){
				a_buffer.value[0]=a_buffer.value[1];
				a_buffer.key[0]=a_buffer.key[1];
			}

			a_buffer.idx--;
			goto end;
		}
	}
	
	page_map_trim(req->key);
end:
	req->end_req(req);
	return 0;
}

uint32_t page_flush(request *const req){
	abort();
	req->end_req(req);
	return 0;
}
static void processing_pending_req(algo_req *req, value_set *v){
	request *parents=req->parents;
	page_param *param=(page_param*)req->param;
	memcpy(param->value->value, &v->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
	parents->type_ftl=parents->type_lower=0;
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
	switch(input->type){
		case DATAW:
			inf_free_valueset(param->value,FS_MALLOC_W);
			break;
		case DATAR:
			fdriver_lock(&rb.pending_lock);
			target_r_iter=rb.pending_req->find(param->value->ppa/L2PGAP);
			for(;target_r_iter->first==param->value->ppa/L2PGAP && 
					target_r_iter!=rb.pending_req->end();){
				pending_req=target_r_iter->second;
				processing_pending_req(pending_req, param->value);
				rb.pending_req->erase(target_r_iter++);
			}
			rb.issue_req->erase(param->value->ppa/L2PGAP);
			fdriver_unlock(&rb.pending_lock);

			fdriver_lock(&rb.read_buffer_lock);
			rb.buffer_ppa=param->value->ppa/L2PGAP;
			memcpy(rb.buffer_value, param->value->value, PAGESIZE);
			fdriver_unlock(&rb.read_buffer_lock);

			if(param->value->ppa%L2PGAP){
				memmove(param->value->value, &param->value->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			}

			break;
	}
	request *res=input->parents;
	if(res){
		res->type_ftl=res->type_lower=0;
		res->end_req(res);//you should call the parents end_req like this
	}
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
