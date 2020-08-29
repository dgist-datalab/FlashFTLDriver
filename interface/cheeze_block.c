#include "cheeze_block.h"
#include "queue.h"
#include "interface.h"
#include "threading.h"
#include "../bench/bench.h"
#include "vectored_interface.h"
#include "../include/utils/crc32.h"
#include <pthread.h>

pthread_t t_id;
extern master_processor mp;

int chrfd;

bool cheeze_end_req(request *const req);
void *ack_to_dev(void*);
queue *ack_queue;
char *null_value;
#ifdef CHECKINGDATA
uint32_t* CRCMAP;
#endif

void init_cheeze(){
	chrfd = open("/dev/cheeze_chr", O_RDWR);
	if (chrfd < 0) {
		perror("Failed to open /dev/cheeze_chr");
		abort();
		return;
	}

	null_value=(char*)malloc(PAGESIZE);
	memset(null_value,0,PAGESIZE);
	q_init(&ack_queue, 128);
#ifdef CHECKINGDATA
	CRCMAP=(uint32_t*)malloc(sizeof(uint32_t) * RANGE);
	memset(CRCMAP, 0, sizeof(uint32_t) *RANGE);
#endif
	pthread_create(&t_id, NULL, &ack_to_dev, NULL);
}

inline void error_check(cheeze_req *creq){
	if(unlikely(creq->size%LPAGESIZE)){
		printf("size not align %s:%d\n", __FILE__, __LINE__);
		abort();
	}
	/*
	if(unlikely(creq->offset%LPAGESIZE)){
		printf("offset not align %s:%d\n", __FILE__, __LINE__);
		abort();
	}*/
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
	res->size=creq->size/LPAGESIZE;
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

				temp->value=inf_get_valueset(&res->buf[LPAGESIZE*i],FS_MALLOC_W,LPAGESIZE);
				break;	
			default:
				printf("error type!\n");
				abort();
				break;
		}
		temp->key=creq->offset+i;

#ifdef CHECKINGDATA
		if(temp->type==FS_SET_T){
			CRCMAP[temp->key]=crc32(&res->buf[LPAGESIZE*i],LPAGESIZE);	
		}
#endif
		printf("REQ-TYPE:%s INFO(%d:%d) LBA: %u\n", type==FS_GET_T?"FS_GET_T":"FS_SET_T",creq->id, i, temp->key);
	}

	return res;
}

bool cheeze_end_req(request *const req){
	vectored_request *preq=req->parents;
	if(req->key==1){
		printf("break!\n");
	}
	switch(req->type){
		case FS_NOTFOUND_T:
			bench_reap_data(req, mp.li);
			printf("%u not found!\n",req->key);
#ifdef CHECKINGDATA
			if(CRCMAP[req->key]){
				printf("\n");
				printf("\t\tcrc checking error in key:%u\n", req->key);	
				printf("\n");		
			}
#endif
			memcpy(&preq->buf[req->seq*LPAGESIZE], null_value,LPAGESIZE);
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FS_GET_T:
			bench_reap_data(req, mp.li);
			if(req->value){
				memcpy(&preq->buf[req->seq*LPAGESIZE], req->value->value,LPAGESIZE);
#ifdef CHECKINGDATA
			if(CRCMAP[req->key]!=crc32(&preq->buf[req->seq*LPAGESIZE], LPAGESIZE)){
				printf("\n");
				printf("\t\tcrc checking error in key:%u\n", req->key);	
				printf("\n");
			}	
#endif

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
		cheeze_req *creq=(cheeze_req*)vec->origin_req;

#ifdef CHECKINGDATA
		if(CRCMAP[creq->offset] && CRCMAP[creq->offset]!=crc32((char*)creq->user_buf, LPAGESIZE)){
				printf("\n");
				printf("\t\tcrc checking error in key:%u, it maybe copy error!\n", creq->offset);	
				printf("\n");
		}
#endif

		r=write(chrfd, vec->origin_req, sizeof(cheeze_req));
		printf("[DONE] REQ INFO(%d) LBA: %u ~ %u\n", creq->id, creq->offset, creq->offset+creq->size/LPAGESIZE-1);
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
#ifdef CHECKINGDATA
	free(CRCMAP);
#endif
	return;
}
