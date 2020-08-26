#include "cheeze_block.h"
#include "queue.h"
#include "interface.h"
#include "threading.h"
#include "../bench/bench.h"
#include "vectored_interface.h"
#include <pthread.h>

pthread_t t_id;
extern master_processor mp;

int chrfd;

bool cheeze_end_req(request *const req);
void *ack_to_dev(void*);
queue *ack_queue;

void init_cheeze(){
	chrfd = open("/dev/cheeze_chr", O_RDWR);
	if (chrfd < 0) {
		perror("Failed to open /dev/cheeze_chr");
		abort();
		return;
	}

	q_init(&ack_queue, 128);
	pthread_create(&t_id, NULL, &ack_to_dev, NULL);
}

inline void error_check(cheeze_req *creq){
	if(unlikely(creq->index%4096)){printf("index not align %s:%d\n", __FILE__, __LINE__);}
	if(unlikely(creq->size%4096)){printf("size not align %s:%d\n", __FILE__, __LINE__);}
	if(unlikely(creq->offset)){printf("offset not align %s:%d\n", __FILE__, __LINE__);}
}

inline FSTYPE decode_type(int rw){
	switch(rw){
		case OP_READ: return FS_GET_T;
		case OP_WRITE: return FS_SET_T;
	}
	return 1;
}

vec_request *get_vectored_request(){
	vec_request *res=(vec_request *)calloc(1, sizeof(vec_request));
	cheeze_req *creq=(cheeze_req*)malloc(sizeof(cheeze_req));
	
	ssize_t r=read(chrfd, creq, sizeof(cheeze_req));
	if(r<0){
		free(res);
		return NULL;
	}

	error_check(creq);

	res->origin_req=(void*)creq;
	res->size=creq->size/4096;
	res->req_array=(request*)calloc(res->size,sizeof(request));
	res->end_req=NULL;
	res->mark=0;

	FSTYPE type=decode_type(creq->rw);

	res->buf=(char*)malloc(creq->size);
	creq->user_buf=res->buf;
	if(type==FS_SET_T){
		r=write(chrfd, creq, sizeof(struct cheeze_req));
		if(r<0){
			free(res);
			return NULL;
		}
	}

	for(uint32_t i=0; i<res->size; i++){
		request *temp=&res->req_array[i];
		temp->parents=res;
		temp->type=type;
		temp->end_req=cheeze_end_req;
		temp->isAsync=ASYNC;
		temp->seq=i;
		switch(type){
			case FS_GET_T:
				temp->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
				break;
			case FS_SET_T:
				temp->value=inf_get_valueset(&res->buf[4096*i],FS_MALLOC_W,4096);
				break;	
			default:
				printf("error type!\n");
				abort();
				break;
		}
		temp->key=creq->index/4096+i;
	}

	return res;
}

bool cheeze_end_req(request *const req){
	vectored_request *preq=req->parents;
	switch(req->type){
		case FS_NOTFOUND_T:
		case FS_GET_T:
			bench_reap_data(req, mp.li);
			if(req->value){
				memcpy(&preq->buf[req->seq*4096], req->value->value,4096);
				inf_free_valueset(req->value,FS_MALLOC_R);
			}
			break;
		case FS_SET_T:
			bench_reap_data(req, mp.li);
			if(req->value) inf_free_valueset(req->value, FS_MALLOC_W);
			break;
		default:
			abort();
	}
	preq->done_cnt++;
	release_each_req(req);

	if(preq->size==preq->done_cnt){
		if(req->type==FS_SET_T){
			free(preq->buf);
			free(preq->origin_req);
			free(preq);
		}
		else{
			while(!q_enqueue((void*)preq, ack_queue)){}
		}
	}
	free(req);
	return true;
}


void *ack_to_dev(void* a){
	vec_request *vec=NULL;
	ssize_t r;
	while(1){
		if(!(vec=(vec_request*)q_dequeue(ack_queue))) continue;
		r=write(chrfd, vec->origin_req, sizeof(cheeze_req));
		if(r<0){
			break;
		}
		free(vec->buf);
		free(vec->origin_req);
		free(vec);
	}
	return NULL;
}

void free_cheeze(){
	return;
}
