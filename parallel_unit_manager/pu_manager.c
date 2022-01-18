#include "../include/settings.h"
#include "../bench/bench.h"
#include "./pu_manager.h"
#include <pthread.h>
#include <unistd.h>
#include <queue>

/*testtest*/
typedef struct dummy_req{
	uint32_t test_ppa;
	uint8_t type;
}dummy_req;

//[seg#,page,chip,bus,card]
#define extract_pu_num(a) ((a>>1)&0x7)

pthread_mutex_t channel_lock=PTHREAD_MUTEX_INITIALIZER;
uint32_t channel_overlap[8];

lower_info pu_manager={
	.create		=	pu_create,
	.destroy	=	pu_destroy,
	.write		=	pu_write,
	.read		=	pu_read,
	.write_sync =	pu_write_sync,
	.read_sync	=	pu_read_sync,
	.device_badblock_checker=NULL,
	.trim_block		=	pu_trim_block,
	.trim_a_block	=	NULL,
	.refresh		=	pu_refresh,
	.stop			=	pu_stop,
	.lower_alloc	=	NULL,
	.lower_free		=	NULL,
	.lower_flying_req_wait	=	pu_flying_req_wait,
	.lower_show_info		=	pu_show_info,
	.lower_tag_num	=	pu_lower_tag_num,
	.print_traffic	=	pu_traffic_print,
	.dump=pu_dump,
	.load=pu_load,
};

static uint8_t test_type(uint8_t type){
	uint8_t t_type=0xff>>1;
	return type&t_type;
}

void pu_traffic_print(lower_info *li){
	for(uint32_t i=0; i<LREQ_TYPE_NUM; i++){
		if(pu_manager.req_type_cnt[i]){
			printf("pu_hit %s: %lu\n",bench_lower_type(i), pu_manager.req_type_cnt[i]);
			pu_manager.req_type_cnt[i]=0;
		}
	}

	lower_info *real_lower=(lower_info*)li->private_data;
	if(real_lower->print_traffic){
		real_lower->print_traffic(real_lower);
	}
}

extern struct lower_info memio_info;
extern struct lower_info aio_info;
extern struct lower_info net_info;
extern struct lower_info my_posix; //posix, posix_memory,posix_async
extern struct lower_info no_info;
extern struct lower_info amf_info;

enum{
	PU_WRITE, PU_READ,
};

typedef struct pu_wrapper{
	uint32_t type;
	algo_req *req;
	uint32_t ppa;
	void *(*end_req)(algo_req*);
	void *param;
	value_set *data;
}pu_wrapper;

pthread_mutex_t pu_cache_lock=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t pu_wrapper_cond=PTHREAD_COND_INITIALIZER;
pthread_mutex_t pu_wrapper_lock=PTHREAD_MUTEX_INITIALIZER;

typedef struct pu_cache{
	uint32_t ppa;
	char page[PAGESIZE];
}pu_cache;

pu_wrapper *pu_wrapper_array;
std::queue<pu_wrapper*>* pu_wrap_q;
pu_cache pu_temp_cache[QDEPTH];

static inline pu_wrapper* get_pu_wrapper(uint32_t type, uint32_t ppa, 
		algo_req *req, void *param, value_set *data, void *(*end_req)(algo_req*)){
	pu_wrapper *res;
	pthread_mutex_lock(&pu_wrapper_lock);
	while(pu_wrap_q->empty()){
		pthread_cond_wait(&pu_wrapper_cond, &pu_wrapper_lock);
	}
	res=pu_wrap_q->front();
	res->type=type;
	res->ppa=ppa;
	res->req=req;
	res->data=data;
	res->param=param;
	res->end_req=end_req;
	pu_wrap_q->pop();
	pthread_mutex_unlock(&pu_wrapper_lock);
	return res;
}

static inline void release_pu_wrapper(pu_wrapper *pw){
	pthread_mutex_lock(&pu_wrapper_lock);
	pu_wrap_q->push(pw);
	pthread_cond_broadcast(&pu_wrapper_cond);
	pthread_mutex_unlock(&pu_wrapper_lock);
}

void *pu_end_req (algo_req *algo_req){
	pu_wrapper *pu=(pu_wrapper*)algo_req->param;


	pthread_mutex_lock(&pu_cache_lock);
	pu_temp_cache[pu->ppa%QDEPTH].ppa=pu->ppa;
	memcpy(pu_temp_cache[pu->ppa%QDEPTH].page, pu->data->value, PAGESIZE);
	pthread_mutex_unlock(&pu_cache_lock);

	algo_req->param=pu->param;
	algo_req->end_req=pu->end_req;
	release_pu_wrapper(pu);
	algo_req->end_req(algo_req);
	return NULL;
}

void pu_create_body(lower_info *_li){
	lower_info *li;
#if defined(posix) || defined(posix_async) || defined(posix_memory)
	li=&my_posix;
#elif defined(bdbm_drv)
	li=&memio_info;
#elif defined(network)
	li=&net_info;
#elif defined(linux_aio)
	li=&aio_info;
#elif defined(no_dev)
	li=&no_info;
#elif defined(AMF)
	li=&amf_info;
#endif
	
	_li->private_data=(void*)li;
	pu_wrap_q=new std::queue<pu_wrapper*>();
	pu_wrapper_array=(pu_wrapper*)calloc(QDEPTH, sizeof(pu_wrapper));
	for(uint32_t i=0; i<QDEPTH; i++){
		pu_wrap_q->push(&pu_wrapper_array[i]);
		pu_temp_cache[i].ppa=UINT32_MAX;
	}
}

uint32_t pu_create(lower_info *_li, blockmanager *bm){
	pu_create_body(_li);
	lower_info *li=(lower_info*)_li->private_data;
	li->create(li, bm);
	return 1;
}

void* pu_destroy(lower_info *li){
	for(uint32_t i=0; i<LREQ_TYPE_NUM; i++){
		if(pu_manager.req_type_cnt[i]){
			printf("pu_hit %s: %lu\n",bench_lower_type(i), pu_manager.req_type_cnt[i]);
			pu_manager.req_type_cnt[i]=0;
		}
	}
	lower_info *real_lower=(lower_info*)li->private_data;
	real_lower->destroy(real_lower);
	free(pu_wrapper_array);
	delete pu_wrap_q;
	return NULL;
}

void* pu_write(uint32_t ppa, uint32_t size, value_set *value,algo_req * const req){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	pu_wrapper *temp_pu_wrapper=get_pu_wrapper(PU_WRITE, ppa, req, req->param, 
			value, req->end_req);
	req->end_req=pu_end_req;
	req->param=(void*)temp_pu_wrapper;
	return real_lower->write(ppa, size, value, req);
}


static bool buffer_hit_check(uint32_t ppa, char *data){
	bool ishit=false;
	pthread_mutex_lock(&pu_cache_lock);
	if(pu_temp_cache[ppa%QDEPTH].ppa==ppa){
		memcpy(data, pu_temp_cache[ppa%QDEPTH].page, PAGESIZE);
		ishit=true;
	}
	pthread_mutex_unlock(&pu_cache_lock);
	return ishit;
}

void* pu_read(uint32_t ppa, uint32_t size, value_set *value,algo_req * const req){
	if(buffer_hit_check(ppa, value->value)){
		if(req->parents){
			req->parents->type_lower=1;
		}
		pu_manager.req_type_cnt[req->type]++;	
		req->end_req(req);
		return NULL;
	}

	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	pu_wrapper *temp_pu_wrapper=get_pu_wrapper(PU_READ, ppa, req, req->param, 
			value, req->end_req);
	req->end_req=pu_end_req;
	req->param=(void *)temp_pu_wrapper;
	return real_lower->read(ppa, size, value, req);
}

void *		pu_write_sync(uint32_t type, uint32_t ppa, char *data){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	return real_lower->write_sync(type, ppa ,data);
}

void *		pu_read_sync(uint32_t type, uint32_t ppa, char *data){
	if(buffer_hit_check(ppa, data)){
		return NULL;
	}
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	return real_lower->read_sync(type, ppa ,data);
}

void* pu_trim_block(uint32_t ppa){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	for(uint32_t i=0; i<QDEPTH; i++){
		pu_temp_cache[i].ppa=UINT32_MAX;
	}
	real_lower->trim_block(ppa);
	return NULL;
}


void* pu_refresh(struct lower_info* li){
	lower_info *real_lower=(lower_info*)li->private_data;
	if(real_lower->refresh){
		real_lower->refresh(real_lower);
	}
	return NULL;
}

void pu_stop(){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	if(real_lower->stop){
		real_lower->stop();
	}
}

void pu_show_info(){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	if(real_lower->lower_show_info){
		real_lower->lower_show_info();
	}
}

uint32_t pu_lower_tag_num(){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	if(real_lower->lower_tag_num){
		return real_lower->lower_tag_num();
	}
	else return 0;
}

void pu_flying_req_wait(){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	if(real_lower->lower_flying_req_wait){
		real_lower->lower_flying_req_wait();
	}
	return;
}

uint32_t	pu_dump(lower_info *li, FILE *fp){
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	return real_lower->dump(real_lower, fp);
}

uint32_t	pu_load(lower_info *_li, FILE *fp){
	pu_create_body(_li);
	lower_info *real_lower=(lower_info*)pu_manager.private_data;
	return real_lower->load(real_lower, fp);
}
