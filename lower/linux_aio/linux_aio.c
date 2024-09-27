#define _LARGEFILE64_SOURCE
#include "../../include/settings.h"
#include "../../bench/bench.h"
#include "../../bench/measurement.h"
#include "../../include/utils/cond_lock.h"
#include "../../include/utils/thpool.h"
#include "../../include/utils/tag_q.h"
#include "../../include/data_struct/partitioned_slab.h"

#include "linux_aio.h"
#include <libaio.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <linux/fs.h>
#include<semaphore.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <semaphore.h>
#include <malloc.h>
void posix_traffic_print(lower_info *li);
void posix_invalidate_inform(uint64_t ppa);

lower_info aio_info={
	.create=aio_create,
	.destroy=aio_destroy,
	.write=aio_write,
	.read=aio_read,
	.write_sync=NULL,
	.read_sync=NULL,
	.device_badblock_checker=NULL,
	.trim_block=aio_trim_block,
	.trim_a_block=aio_trim_a_block,
	.refresh=aio_refresh,
	.stop=aio_stop,
	.lower_alloc=NULL,
	.lower_free=NULL,
	.lower_flying_req_wait=aio_flying_req_wait,
	.lower_show_info=NULL,
	.lower_tag_num=NULL,
	.print_traffic=posix_traffic_print,
	.dump=NULL,
	.load=NULL,
	.invalidate_inform=posix_invalidate_inform,
};

typedef struct aio_private{
	algo_req *req;
	uint32_t tag;
}aio_private;

struct iocb request_list[QDEPTH];
aio_private private_list[QDEPTH];
char *temp_value[QDEPTH];

static int _fd;
static pthread_t t_id;
static io_context_t ctx;
static tag_manager *tm=NULL;
static PS_master *ps_master;

void posix_invalidate_inform(uint64_t ppa){
#ifdef COPYMETA_ONLY
	PS_master_free_slab(ps_master, ppa);
#else
	return;
#endif
}

void posix_traffic_print(lower_info *li){
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

static uint8_t test_type(uint8_t type){
	uint8_t t_type=0xff>>1;
	return type&t_type;
}

void *poller(void *input) {
	aio_private *pri;
	algo_req *req;
	int ret;
	struct io_event done_array[128];
	struct io_event *r;
	struct timespec w_t;
	struct iocb *cb;
	w_t.tv_sec=0;
	w_t.tv_nsec=10*1000;

    for (int i = 0; ; i++) {
		if((ret=io_getevents(ctx,0,128,done_array,&w_t))){
			for(int i=0; i<ret; i++){
				r=&done_array[i];
				cb=r->obj;
				pri=(aio_private*)cb->data;
				if(r->res==(uint32_t)-22){
					printf("error! %s %lu %llu\n",strerror(-r->res),r->res2,cb->u.c.offset);
				}else if(r->res!=PAGESIZE){
					printf("data size error %d!\n",errno);
				}
				req=pri->req;

				req->end_req(req);
				tag_manager_free_tag(tm, pri->tag);
			}
		}
    }
	return NULL;
}

uint32_t aio_create(lower_info *li,blockmanager *bm){
	li->NOB=_NOS;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE * BPS;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(uint32_t);
	li->PPB=_PPB;
	li->PPS=_PPS;
	li->TS=TOTALSIZE;

	li->write_op=li->read_op=li->trim_op=0;
	printf("file name : %s\n", MEDIA_NAME);
	_fd = open(MEDIA_NAME, O_RDWR|O_DIRECT);
	if(_fd==-1){
		printf("file open error!\n");
		exit(1);
	}
	
	printf("!!! posix libaio NOP:%d!!!\n",li->NOP);
	li->write_op=li->read_op=li->trim_op=0;

	int ret=io_setup(QDEPTH,&ctx);
	if(ret!=0){
		printf("io setup error\n");
		exit(1);
	}
	tm=tag_manager_init(QDEPTH);

#ifdef COPYMETA_ONLY
	ps_master=PS_master_init(_NOS, _PPS, _NOP/100*COPYMETA_ONLY);
#else
	for(uint32_t i=0; i<QDEPTH; i++){
		pp_cache[i].ppa=UINT32_MAX;
	}
#endif

	for(uint32_t i=0; i<QDEPTH; i++){
		temp_value[i]=(char*)memalign(4*_K, PAGESIZE);
	}

    pthread_create(&t_id, NULL, &poller, NULL);
	return 1;
}

void *aio_refresh(lower_info *li){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *aio_destroy(lower_info *li){
	posix_traffic_print(li);
	tag_manager_free_manager(tm);
	return NULL;
}


void send_req(FSTYPE type, uint32_t ppa, value_set* value, algo_req *upper_request){
	uint32_t tag=tag_manager_get_tag(tm);

	struct iocb *target_req = &request_list[tag];
	aio_private *pri_req = &private_list[tag];
	pri_req->req = upper_request;
	pri_req->tag =tag;


	switch(type){
		case FS_SET_T:
		io_prep_pwrite(target_req, _fd, (void*)temp_value[tag], PAGESIZE, ppa*PAGESIZE);
		break;
		case FS_GET_T:
		io_prep_pread(target_req, _fd, (void*)temp_value[tag], PAGESIZE, ppa*PAGESIZE);
		break;
	}
	target_req->data=(void*)pri_req;

	io_submit(ctx,1,&target_req);
}

void *aio_write(uint32_t PPA, uint32_t size, value_set* value, algo_req *const req){
	req->ppa = PPA;
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}
	uint8_t t_type=test_type(req->type);
	if(t_type < LREQ_TYPE_NUM){
		aio_info.req_type_cnt[t_type]++;
	}

#ifdef COPYMETA_ONLY
	if(PS_ismeta_data(req->type)){
		PS_master_insert(ps_master, PPA, value->value);
	}
#endif

	send_req(FS_SET_T, PPA, value, req);
	return NULL;
}

void *aio_read(uint32_t PPA, uint32_t size, value_set* value, algo_req *const req){	
	req->ppa = PPA;
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		exit(1);
	}
	
	uint8_t t_type=test_type(req->type);
	if(t_type < LREQ_TYPE_NUM){
		aio_info.req_type_cnt[t_type]++;
	}
	
	send_req(FS_GET_T, PPA, value, req);

	return NULL;
}

void *aio_trim_block(uint32_t PPA){
	return NULL;
}

void *aio_trim_a_block(uint32_t PPA){
	return NULL;
}

void aio_stop(){}

void aio_flying_req_wait(){
	return ;
}
