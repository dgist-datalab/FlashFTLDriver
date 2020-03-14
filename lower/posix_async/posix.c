
#define _LARGEFILE64_SOURCE
#include "posix.h"
#include "../../include/settings.h"
#include "../../bench/bench.h"
#include "../../bench/measurement.h"
#include "../../interface/queue.h"
#include "../../interface/bb_checker.h"
#include "../../algorithm/Lsmtree/lsmtree.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
//#include <readline/readline.h>
//#include <readline/history.h>

static FILE *_fp;
static int _fd;
pthread_mutex_t fd_lock;
#if (ASYNC==1)
queue *p_q;
pthread_t t_id;
bool stopflag;
#endif

lower_info my_posix={
	.create=posix_create,
	.destroy=posix_destroy,
#if (ASYNC==1)
	.write=posix_make_push,
	.read=posix_make_pull,
#elif (ASYNC==0)
	.write=posix_push_data,
	.read=posix_pull_data,
#endif
	.read_hw=NULL,
	.device_badblock_checker=NULL,
#if (ASYNC==1)
	.trim_block=posix_make_trim,
	.trim_a_block=posix_trim_a_block,
#elif (ASYNC==0)
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
	.hw_do_merge=NULL,
	.hw_get_kt=NULL,
	.hw_get_inv=NULL
};

#if (ASYNC==1)
void *l_main(void *__input){
	posix_request *inf_req;

	char thread_name[128]={0};
	sprintf(thread_name,"%s","device_thread");
	prctl(PR_SET_NAME, thread_name);
	pthread_setname_np(t_id,thread_name);

	while(1){
		if(stopflag){
			//printf("posix bye bye!\n");
			pthread_exit(NULL);
			break;
		}
		if(!(inf_req=(posix_request*)q_dequeue(p_q))){
			continue;
		}
		//chang ppa, mapping right ppa from bad block or somthing
		//inf_req->key=bb_checker_fix_ppa(inf_req->key);
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

void *posix_make_push(ppa_t PPA, uint32_t size, value_set* value, bool async, algo_req *const req){
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
			flag=true;
		}
	}
	return NULL;
}

void *posix_make_pull(ppa_t PPA, uint32_t size, value_set* value, bool async, algo_req *const req){
	bool flag=false;
	posix_request *p_req=(posix_request*)malloc(sizeof(posix_request));
	p_req->type=FS_LOWER_R;
	p_req->key=PPA;
	p_req->value=value;
	p_req->upper_req=req;
	p_req->isAsync=async;
	p_req->size=size;

	while(!flag){
		if(q_enqueue((void*)p_req,p_q)){
			flag=true;
		}
	}
	return NULL;
}

void *posix_make_trim(ppa_t PPA, bool async){
	bool flag=false;
	posix_request *p_req=(posix_request*)malloc(sizeof(posix_request));
	p_req->type=FS_LOWER_T;
	p_req->key=PPA;
	p_req->isAsync=async;

	while(!flag){
		if(q_enqueue((void*)p_req,p_q)){
			flag=true;
		}
	}
	return NULL;
}
#endif

uint32_t posix_create(lower_info *li, blockmanager * bm){
	li->NOB=_NOS;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE*BPS;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(ppa_t);
	li->PPB=_PPB;
	li->PPS=_PPS;
	li->TS=TOTALSIZE;

	li->write_op=li->read_op=li->trim_op=0;
	_fd=open(LOWER_FILE_NAME,O_RDWR|O_CREAT|O_TRUNC,0666);
	if(_fd==-1){
		printf("file open error!\n");
		exit(-1);
	}

	_fp=fopen("badblock.txt","r");	
	if(_fp==NULL){
		printf("bb file open error!\n");
		exit(-1);
	}
	pthread_mutex_init(&fd_lock,NULL);
#if (ASYNC==1)
	stopflag = false;
	q_init(&p_q, 1024);
	pthread_create(&t_id,NULL,&l_main,NULL);
#endif
	return 1;
}

void *posix_refresh(lower_info *li){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *posix_destroy(lower_info *li){
	pthread_mutex_destroy(&fd_lock);
	fclose(_fp);
	close(_fd);
#if (ASYNC==1)
	stopflag = true;
	q_free(p_q);
#endif
	return NULL;
}

void *posix_push_data(ppa_t PPA, uint32_t size, value_set* value, bool async,algo_req *const req){
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}
	pthread_mutex_lock(&fd_lock);

	//if(((lsm_params*)req->params)->lsm_type < 6){
	if(lseek64(_fd,((off64_t)my_posix.SOP)*PPA,SEEK_SET)==-1){
		printf("lseek error in write\n");
	}//
	if(!write(_fd,value->value,size)){
		printf("write none!\n");
	}
	//}
	pthread_mutex_unlock(&fd_lock);
	req->end_req(req);
	return NULL;
}

void *posix_pull_data(ppa_t PPA, uint32_t size, value_set* value, bool async,algo_req *const req){	
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}

	pthread_mutex_lock(&fd_lock);

	//if(((lsm_params*)req->params)->lsm_type < 6){
	if(lseek64(_fd,((off64_t)my_posix.SOP)*PPA,SEEK_SET)==-1){
		printf("lseek error in read\n");
	}
	int res;
	if(!(res=read(_fd,value->value,size))){
		printf("%d:read none!\n",res);
	}
	//}
	pthread_mutex_unlock(&fd_lock);

	req->end_req(req);
	/*
	   if(async){
	   req->end_req(req);
	   }
	   else{

	   }*/
	return NULL;
}

void *posix_trim_block(ppa_t PPA, bool async){
	char *temp=(char *)malloc(my_posix.SOB);
	memset(temp,0,my_posix.SOB);
	pthread_mutex_lock(&fd_lock);
	if(lseek64(_fd,((off64_t)my_posix.SOP)*PPA,SEEK_SET)==-1){
		printf("lseek error in trim\n");
	}
	if(!write(_fd,temp,BLOCKSIZE)){
		printf("write none\n");
	}
	pthread_mutex_unlock(&fd_lock);
	free(temp);
	return NULL;
}

extern uint64_t _cnt;
void* posix_badblock_checker(ppa_t ppa, uint32_t size, void*(*process)(uint64_t,uint8_t)){
	static uint64_t bbn=0;//badblock number
	static bool checking_done=false;
	static bool need_dispatch=true;
	return NULL;
	for(int i=0; i<_PPS/_PPB; i++){
		if(checking_done){
			_cnt++;
			continue;
		}
		else if(need_dispatch){
			if(fscanf(_fp,"%lu",&bbn)==EOF){
				checking_done=true;
			}
		}


		if(bbn==ppa){
			need_dispatch=true;
			process(bbn/(1<<14),1);
		}
		else{
			need_dispatch=false;
			_cnt++;
		}
		ppa+=_PPB;
	}
	return NULL;
}

void posix_stop(){}

void posix_flying_req_wait(){
}

void* posix_trim_a_block(uint32_t PPA, bool async){
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}
	my_posix.req_type_cnt[TRIM]++;
	return NULL;
}
