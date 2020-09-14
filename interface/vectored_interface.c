#include "vectored_interface.h"
#include "layer_info.h"
#include "../include/container.h"
#include "../bench/bench.h"
#include "../include/utils/cond_lock.h"
#include "../include/utils/kvssd.h"
#include "../include/utils/tag_q.h"
#include "../include/utils/data_checker.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

extern master_processor mp;
extern tag_manager *tm;
static int32_t flying_cnt = QDEPTH;
static pthread_mutex_t flying_cnt_lock=PTHREAD_MUTEX_INITIALIZER; 
bool vectored_end_req (request * const req);

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

	static uint32_t seq=0;
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
		temp->seq=seq++;
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

		temp->key=*(uint32_t*)buf_parser(buf,&idx,sizeof(uint32_t));
#ifdef CHECKINGDATA
		if(temp->type==FS_SET_T){
			__checking_data_make( temp->key,temp->value->value);
		}
#endif
		temp->offset=*(uint32_t*)buf_parser(buf, &idx, sizeof(uint32_t));
		
	}

	assign_vectored_req(txn);
	return 1;
}

void assign_vectored_req(vec_request *txn){
	while(1){
		pthread_mutex_lock(&flying_cnt_lock);
		if(flying_cnt - (int32_t)txn->size < 0){
			pthread_mutex_unlock(&flying_cnt_lock);
			continue;
		}
		else{
			flying_cnt-=txn->size;
			if(flying_cnt<0){
				printf("abort!!!\n");
				abort();
			}
			pthread_mutex_unlock(&flying_cnt_lock);
		}
		
		if(q_enqueue((void*)txn, mp.processors[0].req_q)){
			break;
		}
	}
}

void release_each_req(request *req){
	uint32_t tag_num=req->tag_num;
	pthread_mutex_lock(&flying_cnt_lock);
	flying_cnt++;
	if(flying_cnt > QDEPTH){
		printf("???\n");
		abort();
	}
	pthread_mutex_unlock(&flying_cnt_lock);

	tag_manager_free_tag(tm, tag_num);
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
		if(mp.stopflag)
			break;
		type=get_next_request(_this, &inf_req, &vec_req);
		if(type==0){
			continue;
		}
		else if(type==1){ //rtry
			inf_req->tag_num=tag_manager_get_tag(tm);
			inf_algorithm_caller(inf_req);	
		}else{
			uint32_t size=vec_req->size;
			for(uint32_t i=0; i<size; i++){
				/*retry queue*/
				while(1){
					request *temp_req=get_retry_request(_this);
					if(temp_req){
						temp_req->tag_num=tag_manager_get_tag(tm);
						inf_algorithm_caller(temp_req);
					}
					else{
						break;
					}
				}
	
				request *req=&vec_req->req_array[i];
				switch(req->type){
					case FS_GET_T:
					case FS_SET_T:
						measure_init(&req->latency_checker);
						measure_start(&req->latency_checker);
					break;
				}
				req->tag_num=tag_manager_get_tag(tm);
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
#ifdef CHECKINGDATA
			__checking_data_check(req->key, req->value->value);
#endif

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

	release_each_req(req);
	preq->done_cnt++;
	if(preq->size==preq->done_cnt){
		if(preq->end_req)
			preq->end_req((void*)preq);	
	}
	return true;
}
