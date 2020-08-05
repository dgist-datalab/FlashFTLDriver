#include "vectored_interface.h"
#include "layer_info.h"
#include "../include/container.h"
#include "../bench/bench.h"
#include "../include/utils/cond_lock.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

extern master_processor mp;
bool vectored_end_req (request * const req);
extern cl_lock *inf_cond, *flying;
/*request-length request-size tid*/
/*type key-len key offset length value*/

inline char *buf_parser(char *buf, uint32_t* idx, uint32_t length){
	char *res=&buf[*idx];
	(*idx)+=length;
	return res;
}

void* inf_transaction_end_req(void *req);
extern bool TXN_debug;
extern char *TXN_debug_ptr;
static uint32_t seq_val;
uint32_t inf_vector_make_req(char *buf, void* (*end_req) (void*), uint32_t mark){
	static uint32_t seq_num=0;
	uint32_t idx=0;
	vec_request *txn=(vec_request*)malloc(sizeof(vec_request));
	//idx+=sizeof(uint32_t);//length;
	txn->tid=*(uint32_t*)buf_parser(buf, &idx, sizeof(uint32_t)); //get tid;
	txn->size=*(uint32_t*)buf_parser(buf, &idx, sizeof(uint32_t)); //request size;


	txn->buf=buf;
	txn->done_cnt=0;
	txn->end_req=end_req;
	txn->mark=mark;
	txn->req_array=(request*)malloc(sizeof(request)*txn->size);

	for(uint32_t i=0; i<txn->size; i++){
		request *temp=&txn->req_array[i];
		temp->tid=txn->tid;
		temp->mark=txn->mark;
		temp->parents=txn;
		temp->type=*(uint8_t*)buf_parser(buf, &idx, sizeof(uint8_t));
		temp->end_req=vectored_end_req;
		temp->params=NULL;
		temp->value=NULL;
		temp->isAsync=ASYNC;
		temp->seq=seq_num++;
		switch(temp->type){
#ifdef KVSSD
			case FS_TRANS_COMMIT:
				temp->tid=*(uint32_t*)buf_parser(buf,&idx, sizeof(uint32_t));
			case FS_TRANS_BEGIN:
			case FS_TRANS_ABORT:
				continue;
#endif
			case FS_GET_T:
				seq_val=temp->seq;
				temp->magic=0;
				temp->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
				break;
			case FS_SET_T:
				temp->value=inf_get_valueset(NULL, FS_MALLOC_W, 4096);
				break;
			default:
				printf("error type!\n");
				abort();
				break;

		}
#ifdef KVSSD
		temp->key.len=*(uint8_t*)buf_parser(buf, &idx, sizeof(uint8_t));
		temp->key.key=buf_parser(buf, &idx, temp->key.len);
#else
		temp->key=*(uint32_t*)buf_parser(buf,&idx,sizeof(uint32_t));
#endif
		temp->offset=*(uint32_t*)buf_parser(buf, &idx, sizeof(uint32_t));
		
	}

	while(1){
		if(q_enqueue((void*)txn, mp.processors[0].req_q)){
			break;
		}
	}
	cl_release(inf_cond);
	return 1;
}


static uint32_t get_next_request(processor *pr, request** inf_req, vec_request **vec_req){
	if(((*inf_req)=(request*)q_dequeue(pr->retry_q))){
		return 1;
	}
	else if(((*vec_req)=(vec_request*)q_dequeue(pr->req_q))){
		return 2;
	}
	return 0;
}

request *get_retry_request(processor *pr){
	void *inf_req=NULL;
	inf_req=q_dequeue(pr->retry_q);

	return (request*)inf_req;
}

void *vectored_main(void *__input){
	vec_request *vec_req;
	request* inf_req;
	processor *_this=NULL;
	for(int i=0; i<1; i++){
		if(pthread_self()==mp.processors[i].t_id){
			_this=&mp.processors[i];
		}
	}
	char thread_name[128]={0};
	sprintf(thread_name,"%s","vecotred_main_thread");
	pthread_setname_np(pthread_self(),thread_name);
	uint32_t type;
	while(1){
		cl_grap(inf_cond);
		if(mp.stopflag)
			break;
		type=get_next_request(_this, &inf_req, &vec_req);
		if(type==0){
			cl_release(inf_cond);
			continue;
		}
		else if(type==1){
			inf_algorithm_caller(inf_req);	
		}else{
			uint32_t size=vec_req->size;
			for(uint32_t i=0; i<size; i++){
				/*retry queue*/
				while(1){
					request *temp_req=get_retry_request(_this);
					if(temp_req){
						inf_algorithm_caller(temp_req);
					}
					else{
						break;
					}
				}
	
				request *req=&vec_req->req_array[i];
				cl_grap(flying);
				switch(req->type){
					case FS_GET_T:
					case FS_SET_T:
						measure_init(&req->latency_checker);
						measure_start(&req->latency_checker);
					break;
				}
				inf_algorithm_caller(req);
			}
		}
	}
	return NULL;
}

bool vectored_end_req (request * const req){
	vectored_request *preq=req->parents;
	switch(req->type){
		case FS_NOTFOUND_T:
		case FS_GET_T:
			bench_reap_data(req, mp.li);
	//		memcpy(req->buf, req->value->value, 4096);
			if(req->value)
				inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FS_SET_T:
			bench_reap_data(req, mp.li);
			if(req->value) inf_free_valueset(req->value, FS_MALLOC_W);
			break;
		default:
			abort();
	}
	preq->done_cnt++;
	if(preq->size==preq->done_cnt){
		if(preq->end_req)
			preq->end_req((void*)preq);	
	}
	cl_release(flying);
	return true;
}
