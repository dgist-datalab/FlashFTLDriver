#define _LARGEFILE64_SOURCE
#include "posix.h"
#include "pipe_lower.h"
#include "../../blockmanager/bb_checker.h"
#include "../../include/settings.h"
#include "../../bench/bench.h"
#include "../../bench/measurement.h"
#include "../../interface/queue.h"
#include "../../interface/bb_checker.h"
#include "../../include/utils/cond_lock.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
//#include <readline/readline.h>
//#include <readline/history.h>
#define LASYNC 0
pthread_mutex_t fd_lock;
mem_seg *seg_table;
#if (LASYNC==1)
queue *p_q;
pthread_t t_id;
bool stopflag;
#endif
#define PPA_LIST_SIZE (240*1024)
cl_lock *lower_flying;
char *invalidate_ppa_ptr;
char *result_addr;

lower_info my_posix={
	.create=posix_create,
	.destroy=posix_destroy,
#if (LASYNC==1)
	.write=posix_make_push,
	.read=posix_make_pull,
#elif (LASYNC==0)
	.write=posix_push_data,
	.read=posix_pull_data,
#endif
	.device_badblock_checker=NULL,
#if (LASYNC==1)
	.trim_block=posix_make_trim,
	.trim_a_block=posix_trim_a_block,
#elif (LASYNC==0)
	.trim_block=posix_trim_block,
	.trim_a_block=posix_trim_a_block,
#endif
	.refresh=posix_refresh,
	.stop=posix_stop,
	.lower_alloc=NULL,
	.lower_free=NULL,
	.lower_flying_req_wait=posix_flying_req_wait,
	.lower_show_info=NULL,
	.lower_tag_num=NULL,
#ifdef Lsmtree
	.read_hw=posix_read_hw,
	.hw_do_merge=posix_hw_do_merge,
	.hw_get_kt=posix_hw_get_kt,
	.hw_get_inv=posix_hw_get_inv
#endif
};

 uint32_t d_write_cnt, m_write_cnt, gcd_write_cnt, gcm_write_cnt;

#if (LASYNC==1)
void *l_main(void *__input){
	posix_request *inf_req;
	while(1){
		cl_grap(lower_flying);
		if(stopflag){
			//printf("posix bye bye!\n");
			pthread_exit(NULL);
			break;
		}
		if(!(inf_req=(posix_request*)q_dequeue(p_q))){
			continue;
		}
		switch(inf_req->type){
			case FS_LOWER_W:
				posix_push_data(inf_req->key, inf_req->size, inf_req->value, inf_req->isAsync, inf_req->upper_req);
				break;
			case FS_LOWER_R:
				posix_pull_data(inf_req->key, inf_req->size, inf_req->value, inf_req->isAsync, inf_req->upper_req);
				break;
			case FS_LOWER_T:
				posix_trim_block(inf_req->key, inf_req->isAsync);
				break;
		}
		free(inf_req);
	}
	return NULL;
}

void *posix_make_push(uint32_t PPA, uint32_t size, value_set* value, bool async, algo_req *const req){
	bool flag=false;
	posix_request *p_req=(posix_request*)malloc(sizeof(posix_request));
	p_req->type=FS_LOWER_W;
	p_req->key=PPA;
	p_req->value=value;
	p_req->upper_req=req;
	p_req->isAsync=async;
	p_req->size=size;

	while(!flag){
		if(q_enqueue((void*)p_req,p_q)){
			cl_release(lower_flying);
			flag=true;
		}

	}
	return NULL;
}

void *posix_make_pull(uint32_t PPA, uint32_t size, value_set* value, bool async, algo_req *const req){
	bool flag=false;
	posix_request *p_req=(posix_request*)malloc(sizeof(posix_request));
	p_req->type=FS_LOWER_R;
	p_req->key=PPA;
	p_req->value=value;
	p_req->upper_req=req;
	p_req->isAsync=async;
	p_req->size=size;
	req->type_lower=0;
	bool once=true;
	while(!flag){
		if(q_enqueue((void*)p_req,p_q)){
			cl_release(lower_flying);
			flag=true;
		}	
		if(!flag && once){
			req->type_lower=1;
			once=false;
		}
	}
	return NULL;
}

void *posix_make_trim(uint32_t PPA, bool async){
	bool flag=false;
	posix_request *p_req=(posix_request*)malloc(sizeof(posix_request));
	p_req->type=FS_LOWER_T;
	p_req->key=PPA;
	p_req->isAsync=async;
	
	while(!flag){
		if(q_enqueue((void*)p_req,p_q)){
			cl_release(lower_flying);
			flag=true;
		}
	}
	return NULL;
}
#endif

uint32_t posix_create(lower_info *li, blockmanager *b){
	li->NOB=_NOS;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE*BPS;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(uint32_t);
	li->PPB=_PPB;
	li->PPS=_PPS;
	li->TS=TOTALSIZE;
	lower_flying=cl_init(QDEPTH,true);
	
	invalidate_ppa_ptr=(char*)malloc(sizeof(uint32_t)*PPA_LIST_SIZE*20);
	result_addr=(char*)malloc(8192*(PPA_LIST_SIZE));

	printf("!!! posix memory LASYNC: %d NOP:%d!!!\n", LASYNC,li->NOP);
	li->write_op=li->read_op=li->trim_op=0;
	seg_table = (mem_seg*)malloc(sizeof(mem_seg)*li->NOP);
	for(uint32_t i = 0; i < li->NOP; i++){
		seg_table[i].storage = NULL;
	}
	pthread_mutex_init(&fd_lock,NULL);
#if (LASYNC==1)
	stopflag = false;
	q_init(&p_q, 1024);
	pthread_create(&t_id,NULL,&l_main,NULL);
#endif

	memset(li->req_type_cnt,0,sizeof(li->req_type_cnt));

	return 1;
}

void *posix_refresh(lower_info *li){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *posix_destroy(lower_info *li){
	for(int i=0; i<LREQ_TYPE_NUM;i++){
		fprintf(stderr,"%s %lu\n",bench_lower_type(i),li->req_type_cnt[i]);
	}
	for(uint32_t i = 0; i < li->NOP; i++){
		free(seg_table[i].storage);
	}
	free(seg_table);
	pthread_mutex_destroy(&fd_lock);
	free(invalidate_ppa_ptr);
	free(result_addr);
#if (LASYNC==1)
	stopflag = true;
	q_free(p_q);
#endif

	return NULL;
}

static uint8_t convert_type(uint8_t type) {
	return (type & (0xff>>1));
}
extern bb_checker checker;
uint32_t convert_ppa(uint32_t PPA){
	return PPA-checker.start_block*_PPS;
}
void *posix_push_data(uint32_t _PPA, uint32_t size, value_set* value, bool async,algo_req *const req){
	uint8_t test_type;
	uint32_t PPA=convert_ppa(_PPA);
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		abort();
	}

	if(my_posix.SOP*PPA >= my_posix.TS){
		printf("\nwrite error\n");
		abort();
	}

	test_type = convert_type(req->type);

	if(test_type < LREQ_TYPE_NUM){
		my_posix.req_type_cnt[test_type]++;
	}

	if(!seg_table[PPA].storage){
		seg_table[PPA].storage = (PTR)malloc(PAGESIZE);
	}
	else{
		abort();
	}
	memcpy(seg_table[PPA].storage,value->value,size);

	req->end_req(req);
	return NULL;
}

void *posix_pull_data(uint32_t _PPA, uint32_t size, value_set* value, bool async,algo_req *const req){
	uint8_t test_type;
	uint32_t PPA=convert_ppa(_PPA);
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}

	if(req->type_lower!=1 && req->type_lower!=0){
		req->type_lower=0;
	}
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		abort();
	}


	if(my_posix.SOP*PPA >= my_posix.TS){
		printf("\nread error\n");
		abort();
	}

	test_type = convert_type(req->type);
	if(test_type < LREQ_TYPE_NUM){
		my_posix.req_type_cnt[test_type]++;
	}
	if(!seg_table[PPA].storage){
		printf("%u not populated!\n",PPA);
		//abort();
	} else {
		memcpy(value->value,seg_table[PPA].storage,size);
	}
	req->type_lower=1;


	req->end_req(req);
	return NULL;
}

void *posix_trim_block(uint32_t _PPA, bool async){
	abort();

	uint32_t PPA=convert_ppa(_PPA);
	char *temp=(char *)malloc(my_posix.SOB);
	memset(temp,0,my_posix.SOB);
	pthread_mutex_lock(&fd_lock);
	if(my_posix.SOP*PPA >= my_posix.TS || PPA%my_posix.PPS != 0){
		printf("\ntrim error\n");
		abort();
	}
	
	my_posix.req_type_cnt[TRIM]++;
	if(seg_table[PPA].storage){
		free(seg_table[PPA/my_posix.PPS].storage);
		seg_table[PPA/my_posix.PPS].storage = NULL;
	}
	pthread_mutex_unlock(&fd_lock);
	free(temp);
	return NULL;
}

void posix_stop(){}

void posix_flying_req_wait(){
#if (LASYNC==1)
	while(p_q->size!=0){}
#endif
}

void* posix_trim_a_block(uint32_t _PPA, bool async){

	uint32_t PPA=convert_ppa(_PPA);
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}
	my_posix.req_type_cnt[TRIM]++;
	for(int i=0; i<_PPB; i++){
		uint32_t t=PPA+i*PUNIT;
		if(!seg_table[t].storage){
			//abort();
		}
		free(seg_table[t].storage);
		seg_table[t].storage=NULL;
	}
	return NULL;
}

void print_array(uint32_t *arr, int num){
	printf("target:");
	for(int i=0; i<num; i++) printf("%d, ",arr[i]);
	printf("\n");
}
#ifdef Lsmtree
uint32_t posix_hw_do_merge(uint32_t lp_num, ppa_t *lp_array, uint32_t hp_num,ppa_t *hp_array,ppa_t *tp_array, uint32_t* ktable_num, uint32_t *invliadate_num){
	if(lp_num==0 || hp_num==0){
		fprintf(stderr,"l:%d h:%d\n",lp_num,hp_num);
		abort();
	}
	pl_body *lp, *hp, *rp;
	lp=plbody_init(seg_table,lp_array,lp_num);
	hp=plbody_init(seg_table,hp_array,hp_num);
	rp=plbody_init(seg_table,tp_array,lp_num+hp_num);
	
	uint32_t lppa, hppa, rppa;
	KEYT lp_key=plbody_get_next_key(lp,&lppa);
	KEYT hp_key=plbody_get_next_key(hp,&hppa);
	KEYT insert_key;
	int next_pop=0;
	int result_cnt=0;
	int invalid_cnt=0;
	char *res_data;
	while(!(lp_key.len==UINT8_MAX && hp_key.len==UINT8_MAX)){
		if(lp_key.len==UINT8_MAX){
			insert_key=hp_key;
			rppa=hppa;
			next_pop=1;
		}
		else if(hp_key.len==UINT8_MAX){
			insert_key=lp_key;
			rppa=lppa;
			next_pop=-1;
		}
		else{
			if(!KEYVALCHECK(lp_key)){
				printf("%.*s\n",KEYFORMAT(lp_key));
				abort();
			}
			if(!KEYVALCHECK(hp_key)){
				printf("%.*s\n",KEYFORMAT(hp_key));
				abort();
			}

			next_pop=KEYCMP(lp_key,hp_key);
			if(next_pop<0){
				insert_key=lp_key;
				rppa=lppa;
			}
			else if(next_pop>0){
				insert_key=hp_key;
				rppa=hppa;
			}
			else{
				memcpy(&invalidate_ppa_ptr[invalid_cnt++*sizeof(uint32_t)],&lppa,sizeof(lppa));
				rppa=hppa;
				insert_key=hp_key;
			}
		}
	
		if((res_data=plbody_insert_new_key(rp,insert_key,rppa,false))){
			if(result_cnt>=hp_num+lp_num){
				printf("%d %d %d\n",result_cnt, hp_num, lp_num);
			}
			memcpy(&result_addr[result_cnt*PAGESIZE],res_data,PAGESIZE);
		//	plbody_data_print(&result_addr[result_cnt*PAGESIZE]);		
			result_cnt++;
		}
		
		if(next_pop<0) lp_key=plbody_get_next_key(lp,&lppa);
		else if(next_pop>0) hp_key=plbody_get_next_key(hp,&hppa);
		else{
			lp_key=plbody_get_next_key(lp,&lppa);
			hp_key=plbody_get_next_key(hp,&hppa);
		}
	}

	if((res_data=plbody_insert_new_key(rp,insert_key,0,true))){
		if(result_cnt>PPA_LIST_SIZE*2){
			printf("too many result!\n");
			abort();
		}
		memcpy(&result_addr[result_cnt*PAGESIZE],res_data,PAGESIZE);
		//	plbody_data_print(&result_addr[result_cnt*PAGESIZE]);		
		result_cnt++;
	}
	*ktable_num=result_cnt;
	*invliadate_num=invalid_cnt;
	free(rp);
	free(lp);
	free(hp);
	return 1;
}

char * posix_hw_get_kt(){
	return result_addr;
}

char *posix_hw_get_inv(){
	return invalidate_ppa_ptr;
}

void* posix_read_hw(uint32_t _PPA, char *key,uint32_t key_len, value_set *value,bool async,algo_req * const req){
	uint32_t PPA=convert_ppa(_PPA);
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}

	if(req->type_lower!=1 && req->type_lower!=0){
		req->type_lower=0;
	}
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		abort();
	}

	pthread_mutex_lock(&fd_lock);

	my_posix.req_type_cnt[MAPPINGR]++;
	if(my_posix.SOP*PPA >= my_posix.TS){
		printf("\nread error\n");
		abort();
	}

	if(!seg_table[PPA].storage){
		printf("%u not populated!\n",PPA);
		abort();
	}
	pthread_mutex_unlock(&fd_lock);
	KEYT temp;
	temp.key=key;
	temp.len=key_len;

	uint32_t res=find_ppa_from(seg_table[PPA].storage,key,key_len);
	if(res==UINT32_MAX){
		req->type=UINT8_MAX;
	}
	else{
		req->ppa=res;
	}
	req->end_req(req);
	//memcpy(value->value,seg_table[PPA].storage,size);
	//req->type_lower=1;
	return NULL;
}
#endif
