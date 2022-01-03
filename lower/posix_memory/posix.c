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
//#include <readline/readline.h>
//#include <readline/history.h>
pthread_mutex_t fd_lock;
mem_seg *seg_table;
queue *p_q;
pthread_t t_id;
bool stopflag;
#define PPA_LIST_SIZE (240*1024)
cl_lock *lower_flying;
char *invalidate_ppa_ptr;
char *result_addr;

void posix_traffic_print(lower_info *li);

typedef struct physical_page_cache{
	uint32_t ppa;
}physical_page_cache;

pthread_mutex_t page_cache_lock=PTHREAD_MUTEX_INITIALIZER;
physical_page_cache pp_cache[QDEPTH];

lower_info my_posix={
	.create=posix_create,
	.destroy=posix_destroy,
#ifdef LASYNC
	.write=posix_make_write,
	.read=posix_make_read,
#else
	.write=posix_write,
	.read=posix_read,
#endif
	.write_sync=posix_write_sync,
	.read_sync=posix_read_sync,
	.device_badblock_checker=NULL,
#ifdef LASYNC
	.trim_block=posix_make_trim,
	.trim_a_block=posix_trim_a_block,
#else
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
	.print_traffic=posix_traffic_print,
	.dump=posix_dump,
	.load=posix_load,
};

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
	printf("end\n");
}

uint32_t d_write_cnt, m_write_cnt, gcd_write_cnt, gcm_write_cnt;
uint32_t lower_test_ppa=UINT32_MAX;

static uint8_t convert_type(uint8_t type) {
	return (type & (0xff>>1));
}
void data_copy_from(uint32_t ppa, char *data){
	if(!seg_table[ppa].storage){
		printf("%u not populated!\n", ppa );
		abort();
	}
	else{
		memcpy(data, seg_table[ppa].storage, PAGESIZE);
	}
}

void data_copy_to(uint32_t ppa, char *data){
	if(!seg_table[ppa].storage){
		seg_table[ppa].storage = (char *)malloc(PAGESIZE);
	}
	else{
		printf("cannot write! plz write before erase!\n");
		abort();
	}
	memcpy(seg_table[ppa].storage,data,PAGESIZE);
}

#ifdef LASYNC
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
		if(inf_req->isAsync){
			switch(inf_req->type){
				case FS_LOWER_W:
					posix_write(inf_req->key, inf_req->size, inf_req->value, inf_req->upper_req);
					break;
				case FS_LOWER_R:
					posix_read(inf_req->key, inf_req->size, inf_req->value, inf_req->upper_req);
					break;
				case FS_LOWER_T:
					posix_trim_block(inf_req->key);
					break;
			}
			free(inf_req);
		}
		else{
			switch(inf_req->type){
				case FS_LOWER_W: 
					data_copy_to(inf_req->key, inf_req->data);
					break;
				case FS_LOWER_R:
					data_copy_from(inf_req->key, inf_req->data);
					break;
			}
			fdriver_unlock(&inf_req->lock);
		}
	}
	return NULL;
}

void posix_async_make_req(posix_request *p_req){
	bool flag=false;
	while(!flag){
		if(q_enqueue((void*)p_req,p_q)){
			cl_release(lower_flying);
			flag=true;
		}	
	}
}

posix_request* posix_get_preq(FSTYPE type, uint32_t PPA, value_set *value, char *data, bool async,
		algo_req *const req){
	posix_request *p_req=(posix_request*)calloc(1, sizeof(posix_request));
	p_req->type=type;
	p_req->key=PPA;
	p_req->value=value;
	p_req->upper_req=req;
	p_req->data=data;
	p_req->isAsync=async;
	if(!p_req->isAsync){
		fdriver_mutex_init(&p_req->lock);
		fdriver_lock(&p_req->lock);
	}
	return p_req;
}

void *posix_make_write(uint32_t PPA, uint32_t size, value_set* value, algo_req *const req){
	posix_request *p_req=posix_get_preq(FS_LOWER_W, PPA, value, NULL, true, req);
	posix_async_make_req(p_req);
	return NULL;
}

void *posix_make_read(uint32_t PPA, uint32_t size, value_set* value, algo_req *const req){
	posix_request *p_req=posix_get_preq(FS_LOWER_R, PPA, value, NULL, true, req);
	posix_async_make_req(p_req);
	return NULL;
}

void *posix_make_trim(uint32_t PPA){
	posix_request *p_req=posix_get_preq(FS_LOWER_T, PPA, NULL, NULL, true, NULL);
	posix_async_make_req(p_req);
	return NULL;
}
#endif

static uint32_t posix_create_body(lower_info *li){
	li->NOB=_NOS;
	li->NOP=_NOP;
	li->SOB=BLOCKSIZE*BPS;
	li->SOP=PAGESIZE;
	li->SOK=sizeof(uint32_t);
	li->PPB=_PPB;
	li->PPS=_PPS;
	li->TS=TOTALSIZE;
	lower_flying=cl_init(QDEPTH,true);
	
	//invalidate_ppa_ptr=(char*)malloc(sizeof(uint32_t)*PPA_LIST_SIZE*20);

#ifdef LASYNC
	printf("!!! (ASYNC) posix memory NOP:%d!!!\n",li->NOP);
#else
	printf("!!! (SYNC) posix memory NOP:%d!!!\n",li->NOP);
#endif
	li->write_op=li->read_op=li->trim_op=0;
	seg_table = (mem_seg*)malloc(sizeof(mem_seg)*li->NOP);
	for(uint32_t i = 0; i < li->NOP; i++){
		seg_table[i].storage = NULL;
	}
	pthread_mutex_init(&fd_lock,NULL);
#ifdef LASYNC
	stopflag = false;
	q_init(&p_q, 1024);
	pthread_create(&t_id,NULL,&l_main,NULL);
#endif

	for(uint32_t i=0; i<QDEPTH; i++){
		pp_cache[i].ppa=UINT32_MAX;
	}
	return 1;
}

uint32_t posix_create(lower_info *li, blockmanager *b){
	posix_create_body(li);
	memset(li->req_type_cnt,0,sizeof(li->req_type_cnt));
	return 1;
}

uint32_t posix_dump(lower_info *li, FILE *fp){
	uint64_t temp_NOP=_NOP;
	fwrite(&temp_NOP,sizeof(uint64_t), 1, fp);
	std::queue<uint32_t> empty_list;
	for(uint32_t i=0; i<li->NOP; i++){
		if(seg_table[i].storage==NULL){
			empty_list.push(i);
		}
	}
	uint32_t empty_list_size=empty_list.size();
	fwrite(&empty_list_size,sizeof(uint32_t), 1, fp);

	for(uint32_t i=0; i<empty_list_size; i++){
		uint32_t target_ppa=empty_list.front();
		fwrite(&target_ppa,sizeof(uint32_t), 1, fp);
		empty_list.pop();
	}

	for(uint32_t i=0; i<li->NOP; i++){
		if(seg_table[i].storage!=NULL){
			fwrite(seg_table[i].storage, 1, PAGESIZE, fp);
		}
	}

	fwrite(li->req_type_cnt, sizeof(uint64_t), LREQ_TYPE_NUM, fp);
	return 1;
}

uint32_t posix_load(lower_info *li, FILE *fp){
	posix_create_body(li);

	uint64_t now_NOP;
	fread(&now_NOP, sizeof(uint64_t), 1, fp);
	if(now_NOP!=_NOP){
		EPRINT("device setting is differ", true);
	}

	uint32_t empty_list_size=0;
	fread(&empty_list_size, sizeof(uint32_t), 1, fp);

	uint32_t *empty_list=(uint32_t *)malloc(sizeof(uint32_t) * 
			empty_list_size);
	fread(empty_list, sizeof(uint32_t), empty_list_size, fp);
	
	std::set<uint32_t> temp_set;
	for(uint32_t j=0; j<empty_list_size; j++){
		temp_set.insert(empty_list[j]);
	}

	for(uint32_t i=0; i<li->NOP; i++){
		if(temp_set.find(i)!=temp_set.end()) {
			seg_table[i].storage=NULL;
			continue;
		}
		seg_table[i].storage=(char*)malloc(PAGESIZE);
		fread(seg_table[i].storage, 1, PAGESIZE, fp);
	}

	fread(li->req_type_cnt, sizeof(uint64_t), LREQ_TYPE_NUM, fp);
	return 1;
}

void *posix_refresh(lower_info *li){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void *posix_destroy(lower_info *li){
	posix_traffic_print(li);
	for(uint32_t i = 0; i < li->NOP; i++){
		free(seg_table[i].storage);
	}
	free(seg_table);
	pthread_mutex_destroy(&fd_lock);
	free(invalidate_ppa_ptr);
#ifdef LASYNC
	stopflag = true;
	q_free(p_q);
#endif

	return NULL;
}

extern bb_checker checker;
inline uint32_t convert_ppa(uint32_t PPA){
	return PPA;
}
void *posix_write(uint32_t _PPA, uint32_t size, value_set* value,algo_req *const req){
	uint32_t PPA=convert_ppa(_PPA);
	if(PPA==lower_test_ppa){
		printf("%u (piece:%u) target write\n", lower_test_ppa, lower_test_ppa*2);
	}
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

	if(collect_io_type(req->type, &my_posix)){
		data_copy_to(PPA, value->value);
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
		data_copy_from(PPA, value->value);
	}

	req->end_req(req);
	return NULL;
}

void *posix_write_sync(uint32_t type, uint32_t ppa, char *data){
	if(collect_io_type(type, &my_posix)){
#ifdef LASYNC
		posix_request *p_req=posix_get_preq(FS_LOWER_W, ppa, NULL, data, false, NULL);
		posix_async_make_req(p_req);
		fdriver_lock(&p_req->lock);
		free(p_req);
#else
		data_copy_to(ppa, data);
#endif
	}
	return NULL;
}

void *posix_read_sync(uint32_t type, uint32_t ppa, char *data){
	if(collect_io_type(type, &my_posix)){
#ifdef LASYNC
		posix_request *p_req=posix_get_preq(FS_LOWER_R, ppa, NULL, data, false, NULL);
		posix_async_make_req(p_req);
		fdriver_lock(&p_req->lock);
		free(p_req);
#else
		data_copy_from(ppa, data);
#endif
	}
	return NULL;
}

void *posix_trim_block(uint32_t _PPA){
	uint32_t PPA=convert_ppa(_PPA);
	if(my_posix.SOP*PPA >= my_posix.TS || PPA%my_posix.PPS != 0){
		printf("\ntrim error\n");
		abort();
	}
	
	my_posix.req_type_cnt[TRIM]++;
	for(uint32_t i=PPA; i<PPA+my_posix.PPS; i++){
		free(seg_table[i].storage);	
		if(PPA==lower_test_ppa){
			printf("%u (piece:%u) target trim\n", lower_test_ppa, lower_test_ppa*2);
		}

		seg_table[i].storage=NULL;
	}
	return NULL;
}

void posix_stop(){}

void posix_flying_req_wait(){
#ifdef LASYNC
	while(p_q->size!=0){}
#endif
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
