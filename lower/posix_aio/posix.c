#define _LARGEFILE64_SOURCE
#include "posix.h"
#include "../../blockmanager/bb_checker.h"
#include "../../include/settings.h"
#include "../../bench/bench.h"
#include "../../bench/measurement.h"
#include "../../interface/queue.h"
#include "../../interface/bb_checker.h"
#include "../../include/utils/tag_q.h"
#include "../../include/debug_utils.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <limits.h>
#include <queue>
#include <set>

#include <liburing.h>
#define FILENAME "test.data"
int fd;
PS_master *ps_master;
pthread_t t_id;
bool stopflag;
#define PPA_LIST_SIZE (240*1024)

void posix_traffic_print(lower_info *li);

typedef struct physical_page_cache{
	uint32_t ppa;
}physical_page_cache;

pthread_mutex_t page_cache_lock=PTHREAD_MUTEX_INITIALIZER;
physical_page_cache pp_cache[QDEPTH];

lower_info my_posix={
	.create=posix_create,
	.destroy=posix_destroy,
	.write=posix_write,
	.read=posix_read,
	.write_sync=posix_write_sync,
	.read_sync=posix_read_sync,
	.device_badblock_checker=NULL,
	.trim_block=posix_trim_block,
	.trim_a_block=posix_trim_a_block,
	.refresh=posix_refresh,
	.stop=posix_stop,
	.lower_alloc=NULL,
	.lower_free=NULL,
	.lower_flying_req_wait=posix_flying_req_wait,
	.lower_show_info=NULL,
	.lower_tag_num=NULL,
	.print_traffic=posix_traffic_print,
	.dump=posix_dump,
	.load=posix_load,
	.invalidate_inform=posix_invalidate_inform,
};

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

uint32_t d_write_cnt, m_write_cnt, gcd_write_cnt, gcm_write_cnt;
uint32_t lower_test_ppa=UINT32_MAX;

static uint8_t convert_type(uint8_t type) {
	return (type & (0xff>>1));
}

io_uring ring;
tag_manager *tm=nullptr;
posix_request request_list[QDEPTH];

void *uring_poller(void *arg){
	while(true){
		io_uring_cqe *cqe;
		int ret = io_uring_wait_cqes(&ring, NULL, 0, NULL, NULL);
		if (ret < 0) {
			perror("io_uring_wait_cqes");
		}
		uint32_t i=0;
		io_uring_for_each_cqe(&ring, i, cqe) {
        	if (cqe->res < 0) {
            	fprintf(stderr, "Error: %d\n", cqe->res);
        	} else {
				posix_request *req = (posix_request*)io_uring_cqe_get_data(cqe);
				req->upper_req->end_req(req->upper_req);
				tag_manager_free_tag(tm, req->tag);
        	}
    	}
		io_uring_cq_advance(&ring, i);
	}
	return NULL;
}

static uint32_t posix_create_body(lower_info *li){
	li->NOB=_NOS;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE*BPS;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(uint32_t);
	li->PPB=_PPB;
	li->PPS=_PPS;
	li->TS=TOTALSIZE;
	
	fd = open(FILENAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	tm = tag_manager_init(QDEPTH);

 	if (io_uring_queue_init(QDEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        close(fd);
        return 1;
    }
	
	printf("!!! posix AIO NOP:%d!!!\n",li->NOP);
	li->write_op=li->read_op=li->trim_op=0;

#ifdef COPYMETA_ONLY
	ps_master=PS_master_init(_NOS, _PPS, _NOP/100*COPYMETA_ONLY);
#else
	for(uint32_t i=0; i<QDEPTH; i++){
		pp_cache[i].ppa=UINT32_MAX;
	}
#endif


    pthread_create(&t_id, NULL,uring_poller, NULL);
	return 1;
}

uint32_t posix_create(lower_info *li, blockmanager *b){
	posix_create_body(li);
	memset(li->req_type_cnt,0,sizeof(li->req_type_cnt));
	return 1;
}

uint32_t posix_dump(lower_info *li, FILE *fp){
	return 1;
}

uint32_t posix_load(lower_info *li, FILE *fp){
	return 1;
}

void *posix_refresh(lower_info *li){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *posix_destroy(lower_info *li){
	posix_traffic_print(li);
	tag_manager_free_manager(tm);
	return NULL;
}

extern bb_checker checker;
inline uint32_t convert_ppa(uint32_t PPA){
	return PPA;
}

void send_req(FSTYPE type, uint32_t ppa, value_set *value, algo_req *upper_request){
	uint32_t tag=tag_manager_get_tag(tm);
	posix_request *target_req = &request_list[tag];
	target_req->type=type;
	target_req->upper_req=upper_request;
	target_req->ppa=ppa;
	io_uring_sqe *sqe = io_uring_get_sqe(&ring);

	switch (type){
		case FS_SET_T:
		io_uring_prep_write(sqe, fd, value->value, ppa*PAGESIZE, PAGESIZE);
		break;
		case FS_GET_T:
		io_uring_prep_read(sqe, fd, value->value, ppa*PAGESIZE, PAGESIZE);
		break;
	}
	io_uring_sqe_set_data(sqe, target_req);
	io_uring_submit(&ring);
}


void *posix_write(uint32_t _PPA, uint32_t size, value_set* value,algo_req *const req){
	uint32_t PPA=convert_ppa(_PPA);
	if(PPA==lower_test_ppa){
		printf("%u (piece:%u) target write\n", lower_test_ppa, lower_test_ppa*2);
	}
	if(PPA>_NOP){
		//printf("address error!\n");
		//abort();
	}
	else{
		if (value->dmatag == -1)
		{
			printf("dmatag -1 error!\n");
			abort();
		}

		if (my_posix.SOP * PPA >= my_posix.TS)
		{
			printf("\nwrite error\n");
			abort();
		}

		if (collect_io_type(req->type, &my_posix))
		{
			send_req(FS_SET_T, req->ppa, value, req);
		}
	}

	req->end_req(req);
	return NULL;
}

void *posix_read(uint32_t _PPA, uint32_t size, value_set* value, algo_req *const req){
	uint32_t PPA=convert_ppa(_PPA);
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}
	if(PPA==lower_test_ppa){
		printf("%u (piece:%u) target write\n", lower_test_ppa, lower_test_ppa*2);
	}
	if(value->dmatag==-1){
		printf("dmatag -1 error!\n");
		abort();
	}

	if(my_posix.SOP*PPA >= my_posix.TS){
		printf("\nread error\n");
		abort();
	}

	if(collect_io_type(req->type, &my_posix)){
		send_req(FS_GET_T, req->ppa, value, req);
	}

	req->end_req(req);
	return NULL;
}

void *posix_write_sync(uint32_t type, uint32_t ppa, char *data){
	if(collect_io_type(type, &my_posix)){
		//data_copy_to(ppa, data, type);
	}
	return NULL;
}

void *posix_read_sync(uint32_t type, uint32_t ppa, char *data){
	if(collect_io_type(type, &my_posix)){
		//data_copy_from(ppa, data, type);
	}
	return NULL;
}

void *posix_trim_block(uint32_t _PPA){
	uint32_t PPA=convert_ppa(_PPA);

#ifdef COPYMETA_ONLY
	PS_master_free_partition(ps_master, PPA);
#endif

	if(my_posix.SOP*PPA >= my_posix.TS || PPA%my_posix.PPS != 0){
		printf("\ntrim error\n");
		abort();
	}
	
	my_posix.req_type_cnt[TRIM]++;
	for(uint32_t i=PPA; i<PPA+my_posix.PPS; i++){
		if(PPA==lower_test_ppa){
			printf("%u (piece:%u) target trim\n", lower_test_ppa, lower_test_ppa*2);
		}
	}
	return NULL;
}

void posix_stop(){}

void posix_flying_req_wait(){
}

void* posix_trim_a_block(uint32_t _PPA){

	uint32_t PPA=convert_ppa(_PPA);
	if(PPA>_NOP){
		printf("address error!\n");
		abort();
	}
	my_posix.req_type_cnt[TRIM]++;
	for(int i=0; i<_PPB; i++){
		uint32_t t=PPA+i*PUNIT;
	}
	return NULL;
}

void print_array(uint32_t *arr, int num){
	printf("target:");
	for(int i=0; i<num; i++) printf("%d, ",arr[i]);
	printf("\n");
}
