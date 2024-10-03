#include "../../include/settings.h"
#include "../../include/container.h"
#include "../../include/data_struct/partitioned_slab.h"
#include "frontend/libmemio/libmemio.h"
#include "bdbm_inf.h"
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
pthread_mutex_t test_lock;
PS_master *ps_master;
memio_t *mio;


typedef struct memio_wrapper{
	uint32_t tag;
	uint32_t cnt;
	uint32_t ppa;
	algo_req *org;
}amf_wrapper;

pthread_cond_t wrapper_cond=PTHREAD_COND_INITIALIZER;
pthread_mutex_t wrapper_lock=PTHREAD_MUTEX_INITIALIZER;

algo_req *wrapper_array;
memio_wrapper *mem_wrap_array;
std::queue<uint32_t>* wrap_q;

static inline algo_req* get_memio_wrapper(uint32_t ppa, algo_req* org, bool sync){
	algo_req *res;
	pthread_mutex_lock(&wrapper_lock);
	while(wrap_q->empty()){
		pthread_cond_wait(&wrapper_cond, &wrapper_lock);
	}
	uint32_t tag_num=wrap_q->front();
	res=&wrapper_array[tag_num];
	memio_wrapper *pri=&mem_wrap_array[tag_num];
	pri->org=org;
	pri->cnt=0;
	pri->ppa=ppa;
	pri->tag=tag_num;

	res->param=(void*)pri;

	wrap_q->pop();
	pthread_mutex_unlock(&wrapper_lock);
	return res;
}

static inline void release_memio_wrapper(uint32_t tag){
	pthread_mutex_lock(&wrapper_lock);
	wrap_q->push(tag);
	pthread_cond_broadcast(&wrapper_cond);
	pthread_mutex_unlock(&wrapper_lock);
}


void *wrap_end_req(algo_req* req){
	memio_wrapper *pri=(memio_wrapper*)req->param;
	pri->cnt++;
	if(pri->cnt==2){
		algo_req *req=pri->org;
		req->end_req(req);
		release_memio_wrapper(pri->tag);
	}
	return NULL;
}

void invalidate_inform(uint64_t ppa);
static void traffic_print(lower_info *li){
	static int cnt=0;
	printf("info print %u\n",cnt);
	for(int i=0; i<LREQ_TYPE_NUM;i++){
		printf("%s %lu\n",bench_lower_type(i),li->req_type_cnt[i]);
	}
	printf("WAF: %lf\n\n",
		   (double)(li->req_type_cnt[MAPPINGW] +
					li->req_type_cnt[DATAW] +
					li->req_type_cnt[GCDW] +
					li->req_type_cnt[GCMW_DGC] +
					li->req_type_cnt[GCMW] +
					li->req_type_cnt[COMPACTIONDATAW]) /
			   li->req_type_cnt[DATAW]);
	memset(li->req_type_cnt, 0,  sizeof(uint64_t)*LREQ_TYPE_NUM);
	printf("end\n");
}

lower_info memio_info={
	.create=memio_info_create,
	.destroy=memio_info_destroy,
	.write=memio_info_write_data,
	.read=memio_info_read_data,
	.write_sync=NULL,
	.read_sync=NULL,
	.device_badblock_checker=memio_badblock_checker,
	.trim_block=memio_info_trim_block,
	.trim_a_block=memio_info_trim_a_block,
	.refresh=memio_info_refresh,
	.stop=memio_info_stop,
	.lower_alloc=NULL,
	.lower_free=NULL,
	.lower_flying_req_wait=memio_flying_req_wait,
	.lower_show_info=memio_show_info_,
	.lower_tag_num=memio_tag_num,
	.print_traffic=traffic_print,
	.dump=NULL,
	.load=NULL,
	.invalidate_inform=invalidate_inform,
};

void invalidate_inform(uint64_t ppa){
#ifdef COPYMETA_ONLY
	PS_master_free_slab(ps_master, ppa);
#else
	return;
#endif
}

uint32_t memio_info_create(lower_info *li, blockmanager *bm){
	li->NOB=_NOB;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(uint32_t);
	li->PPB=_PPB;
	li->TS=TOTALSIZE;

	li->write_op=li->read_op=li->trim_op=0;
	pthread_mutex_init(&test_lock, 0);
	pthread_mutex_lock(&test_lock);
	
	memset(li->req_type_cnt,0,sizeof(li->req_type_cnt));
	mio=memio_open();
	
	li->bm=bm;

#ifdef COPYMETA_ONLY
	ps_master=PS_master_init(_NOS, _PPS, _NOP/100*COPYMETA_ONLY);
#endif

	wrapper_array=(algo_req*)malloc(sizeof(algo_req)*QDEPTH);
	mem_wrap_array=(memio_wrapper*)malloc(sizeof(memio_wrapper)*QDEPTH);
	wrap_q=new std::queue<uint32_t>();
	for(uint32_t i=0; i<QDEPTH; i++){
		wrapper_array[i].end_req=wrap_end_req;
		wrap_q->push(i);
	}
	return 1;
}

static uint8_t test_type(uint8_t type){
	uint8_t t_type=0xff>>1;
	return type&t_type;
}
void *memio_info_destroy(lower_info *li){
	for(int i=0; i<LREQ_TYPE_NUM;i++){
		fprintf(stderr,"%s %lu\n",bench_lower_type(i),li->req_type_cnt[i]);
	}

    fprintf(stderr,"Total Read Traffic : %lu\n", li->req_type_cnt[1]+li->req_type_cnt[3]+li->req_type_cnt[5]+li->req_type_cnt[7]);
    fprintf(stderr,"Total Write Traffic: %lu\n\n", li->req_type_cnt[2]+li->req_type_cnt[4]+li->req_type_cnt[6]+li->req_type_cnt[8]);
    fprintf(stderr,"Total WAF: %.2f\n\n", (float)(li->req_type_cnt[2]+li->req_type_cnt[4]+li->req_type_cnt[6]+li->req_type_cnt[8]) / li->req_type_cnt[6]);

	li->write_op=li->read_op=li->trim_op=0;
	memio_close(mio);
	return NULL;
}

static inline void __issue(uint32_t type, algo_req *org_req, uint32_t ppa){
	algo_req *temp_req=get_memio_wrapper(ppa, org_req, false);
	uint32_t target_ppa;
	for(uint32_t i=0; i<2; i++){
		target_ppa=(ppa*2+i);
		switch(type){
			case 1:
				memio_empty_write(mio, target_ppa, (void*)temp_req);
				break;
			case 2:
				memio_empty_read(mio, target_ppa, (void*)temp_req);
				break;
		}
	}
}

void *memio_info_write_data(uint32_t ppa, uint32_t size, value_set *value, algo_req *const req){
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}
	
	collect_io_type(req->type, &memio_info);
	if(PS_ismeta_data(req->type)){
		PS_master_insert(ps_master,ppa,value->value);
#ifdef NO_MEMCPY_DATA
		value->value=NULL;
		value->free_unavailable=true;
#endif
	}

	//memio_write(mio,ppa,(uint32_t)size,(uint8_t*)value->value,async,(void*)req,value->dmatag);
	//memio_empty_write(mio,ppa,(void*)req);
	__issue(1, req, ppa);
	return NULL;
}
void *memio_info_read_data(uint32_t ppa, uint32_t size, value_set *value, algo_req *const req){
	
	collect_io_type(req->type, &memio_info);

	//memio_empty_read(mio,ppa,(void*)req);
	//memio_read(mio,ppa,(uint32_t)size,(uint8_t*)value->value,async,(void*)req,value->dmatag);

	if(PS_ismeta_data(req->type)){
		char *temp = PS_master_get(ps_master,ppa);
		if(temp==NULL){
			printf("target data null: %s:%u\n", __FILE__, __LINE__);
		}
		#ifdef NO_MEMCPY_DATA
		free(value->value);
		value->value=temp;
		value->free_unavailable=true;
		#else
		memcpy(value->value,temp,PAGESIZE);
		#endif
	}
	__issue(2, req, ppa);
	return NULL;
}

void *memio_info_trim_block(uint32_t ppa){
	collect_io_type(TRIM, &memio_info);
	memio_trim(mio, ppa*2, (1<<14)*8192, NULL);
	memio_trim(mio, ppa*2+16384, (1<<14)*8192, NULL);
	
#ifdef COPYMETA_ONLY
	PS_master_free_partition(ps_master, ppa);
#endif

	memio_info.req_type_cnt[TRIM]++;
	return NULL;
}

void *memio_info_refresh(struct lower_info* li){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *memio_badblock_checker(uint32_t ppa,uint32_t size, void*(*process)(uint64_t,uint8_t)){
	memio_trim(mio,ppa,size,process);
	return NULL;
}


void *memio_info_trim_a_block(uint32_t ppa){
	collect_io_type(TRIM, &memio_info);
	memio_trim_a_block(mio,ppa);
	return NULL;
}


void memio_info_stop(){}

void memio_flying_req_wait(){
	memio_tag_num();
	while(!memio_is_clean(mio));
}

void memio_show_info_(){
	memio_show_info();
}

uint32_t memio_tag_num(){
	return mio->tagQ->size();
}
