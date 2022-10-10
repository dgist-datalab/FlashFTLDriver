#include "cheeze_hg_block.h"
#include "queue.h"
#include "interface.h"
#include "threading.h"
#include "../bench/bench.h"
#include "vectored_interface.h"
#include "../include/utils/crc32.h"
#include "../include/debug_utils.h"
#include <pthread.h>

extern master_processor mp;


#define TOTAL_SIZE (3ULL *1024L *1024L *1024L)
#define TRACE_DEV_SIZE (128ULL * 1024L * 1024L * 1024L)
#define CRC_BUFSIZE (2ULL * 1024L * 1024L)

static uint64_t PHYS_ADDR=0x3800000000;
static void *page_addr;
static uint8_t *send_event_addr; // CHEEZE_QUEUE_SIZE ==> 16B
static uint8_t *recv_event_addr; // 16B
static uint64_t *seq_addr; // 8KB
struct cheeze_req_user *ureq_addr; // sizeof(req) * 1024
static char *data_addr[2]; // page_addr[1]: 1GB, page_addr[2]: 1GB
static uint64_t seq = 0;
int trace_fd = 0;
fdriver_lock_t trace_fd_lock;
extern uint32_t test_key;
static volatile uint64_t issue_req_num=0;
static volatile uint64_t end_req_num=0;
bool print_read_latency=false;
//static uint32_t trace_crc[TRACE_DEV_SIZE/LPAGESIZE];
//static uint32_t trace_crc_buf[CRC_BUFSIZE];

_request_monitor request_monitor;
void request_print_log(){
#ifdef TRACE_COLLECT
	fdriver_lock(&trace_fd_lock);
	cheeze_ureq temp;
	temp.id=UINT32_MAX;
	write(trace_fd, &temp, sizeof(cheeze_ureq));
	fdriver_unlock(&trace_fd_lock);
#endif

	printf("requst log\n");
	printf("write seq(avg_seq,cnt): %.2lf %u\n",
			(float)request_monitor.write_sequential_length/request_monitor.write_sequential_cnt, request_monitor.write_sequential_cnt);
	printf("read seq(avg_seq,cnt): %.2lf %u\n",
			(float)request_monitor.read_sequential_length/request_monitor.read_sequential_cnt, request_monitor.read_sequential_cnt);
	printf("write read random: %u %u\n", request_monitor.write_random_cnt,
			request_monitor.read_random_cnt);
	double total_write_length=(double)(request_monitor.write_sequential_length+request_monitor.write_random_cnt)/(request_monitor.write_sequential_cnt+request_monitor.write_random_cnt);
	double total_read_length=(double)(request_monitor.read_sequential_length+request_monitor.read_random_cnt)/(request_monitor.read_sequential_cnt+request_monitor.read_random_cnt);

	uint32_t request_length=request_monitor.write_sequential_length+request_monitor.write_random_cnt+request_monitor.read_sequential_length+request_monitor.read_random_cnt;
	printf("trim avg size: %.2lf\n", (double)request_monitor.trim_length/request_monitor.trim_length);
	printf("trim ratio :%.2lf\n", (double)request_monitor.trim_length/(request_monitor.trim_length+request_length));
	printf("total avg_seq w:%lf, r:%lf\n", total_write_length, total_read_length);

}

void request_memset_print_log(){
	memset(&request_monitor, 0, sizeof(request_monitor));
}

extern pthread_mutex_t req_cnt_lock;


static inline char *get_buf_addr(char **pdata_addr, int id) {
    int idx = id / ITEMS_PER_HP;
    return pdata_addr[idx] + ((id % ITEMS_PER_HP) * CHEEZE_BUF_SIZE);
}

static void shm_meta_init(void *ppage_addr) {
    memset(ppage_addr, 0, (1ULL * 1024 * 1024 * 1024));
    send_event_addr = (uint8_t*)(ppage_addr + SEND_OFF); // CHEEZE_QUEUE_SIZE ==> 16B
    recv_event_addr =(uint8_t*)(ppage_addr + RECV_OFF); // 16B
    seq_addr = (uint64_t*)(ppage_addr + SEQ_OFF); // 8KB
    ureq_addr = (cheeze_req_user*)(ppage_addr + REQS_OFF); // sizeof(req) * 1024
}

static void shm_data_init(void *ppage_addr) {
    data_addr[0] = ((char *)ppage_addr) + (1ULL * 1024 * 1024 * 1024);
    data_addr[1] = ((char *)ppage_addr) + (2ULL * 1024 * 1024 * 1024);
}


bool cheeze_end_req(request *const req);
char *null_value;

#if defined(CHECKINGDATA) || defined(TRACE_REPLAY)
uint32_t* CRCMAP;
#endif

#ifdef TRACE_COLLECT
uint32_t* crc_buffer;
#endif

#define TRACE_TARGET "/trace"
#ifdef WRITE_STOP_READ
fdriver_lock_t write_check_lock;
volatile uint32_t write_cnt;
#endif
void init_trace_cheeze(){
#ifdef WRITE_STOP_READ
	fdriver_mutex_init(&trace_fd_lock);
	fdriver_mutex_init(&write_check_lock);
#endif
	trace_fd = open(TRACE_TARGET, O_RDONLY);
	if (trace_fd < 0) {
		perror("Failed to open " TRACE_TARGET);
		abort();
		return;
	}

	null_value = (char*)malloc(PAGESIZE);
	memset(null_value, 0, PAGESIZE);

#if defined(CHECKINGDATA) || defined(TRACE_REPLAY)
	CRCMAP=(uint32_t*)malloc(sizeof(uint32_t) * RANGE);
	memset(CRCMAP, 0, sizeof(uint32_t) *RANGE);
#endif
}

void init_cheeze(uint64_t phy_addr){
#ifdef WRITE_STOP_READ
	fdriver_mutex_init(&trace_fd_lock);
	fdriver_mutex_init(&write_check_lock);
#endif
	printf("QSIZE:%u\n", QSIZE);
	int chrfd = open("/dev/mem", O_RDWR);
	if (chrfd < 0) {
		perror("Failed to open /dev/mem");
		abort();
		return;
	}
	
	if(phy_addr){
		PHYS_ADDR=phy_addr;
	}

	uint64_t pagesize, addr, len;
	pagesize=getpagesize();
	addr = PHYS_ADDR & (~(pagesize - 1));
    len = (PHYS_ADDR & (pagesize - 1)) + TOTAL_SIZE;

	page_addr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, chrfd, addr);
    if (page_addr == MAP_FAILED) {
        perror("Failed to mmap plain device path");
        exit(1);
    }
	close(chrfd);

	shm_meta_init(page_addr);
	shm_data_init(page_addr);
	
	null_value=(char*)malloc(PAGESIZE);
	memset(null_value,0,PAGESIZE);

#if defined(CHECKINGDATA) || defined(TRACE_REPLAY)
	CRCMAP=(uint32_t*)malloc(sizeof(uint32_t) * RANGE);
	memset(CRCMAP, 0, sizeof(uint32_t) *RANGE);
#endif

#ifdef TRACE_COLLECT
	trace_fd=open(TRACE_TARGET, 0x666, O_CREAT | O_WRONLY | O_TRUNC);
	if (trace_fd < 0) {
		perror("Failed to open " TRACE_TARGET);
		abort();
		return;
	}
	crc_buffer=(uint32_t*)malloc(sizeof(uint32_t) * 512);
#endif
}

inline void error_check(cheeze_ureq *creq){
	if(unlikely(creq->len%LPAGESIZE)){
		printf("size not align %s:%d\n", __FILE__, __LINE__);
		abort();
	}
}

inline FSTYPE decode_type(int op){
	switch(op){
		case REQ_OP_READ: return FS_GET_T;
		case REQ_OP_WRITE: return FS_SET_T;
		case REQ_OP_FLUSH: return FS_FLUSH_T;
		case REQ_OP_DISCARD: return FS_DELETE_T;
		default:
			printf("not valid type!\n");
			abort();
	}
	return 1;
}

const char *type_to_str(uint32_t type){
	switch(type){
		case FS_GET_T: return "FS_GET_T";
		case FS_SET_T: return "FS_SET_T";
		case FS_FLUSH_T: return "FS_FLUSH_T";
		case FS_DELETE_T: return "FS_DELETE_T";
	}
	return NULL;
}

static inline vec_request *ch_ureq2vec_req(cheeze_ureq *creq, int id){

	static uint32_t global_vec_seq_id=0;
	vec_request *res=(vec_request *)calloc(1, sizeof(vec_request));
	res->tag_id=id;
	res->seq_id=global_vec_seq_id++;

	error_check(creq);

	res->origin_req=(void*)creq;
	res->size=creq->len/LPAGESIZE;
	res->req_array=(request*)calloc(res->size,sizeof(request));
	res->end_req=NULL;
	res->mark=0;

	FSTYPE type=decode_type(creq->op);
	res->type=type;
	if(type!=FS_GET_T && type!=FS_SET_T){
		res->buf=NULL;
	}
	else{
#ifdef TRACE_REPLAY
		res->buf=NULL;	
#else
		res->buf=get_buf_addr(data_addr, id);
#endif
	}

	if(type==FS_DELETE_T){
		request_monitor.trim_length+=res->size;
		request_monitor.trim_cnt++;
	}

	if(res->size > QSIZE){
		printf("----------------number of reqs is over %u < %u\n", QSIZE, res->size);
		abort();
		return NULL;
	}
	uint32_t prev_lba=UINT32_MAX;
	uint32_t consecutive_cnt=0;
	static uint32_t global_seq=0;

#ifdef WRITE_STOP_READ
	static uint32_t previous_type;
	uint32_t now_type=type;
#endif
	for(uint32_t i=0; i<res->size; i++){
		request *temp=&res->req_array[i];
		temp->parents=res;
		temp->type=type;
		temp->end_req=cheeze_end_req;
		temp->seq=i;
		temp->type_ftl=0;
		temp->type_lower=0;
		temp->is_sequential_start=false;
		temp->flush_all=0;
		temp->global_seq=global_seq++;
		if(i==0){
			if(res->size>1){
				if(type==FS_GET_T){
					request_monitor.read_sequential_cnt++;
					request_monitor.read_sequential_length+=res->size;
				}	
				else if(type==FS_SET_T){
					request_monitor.write_sequential_cnt++;
					request_monitor.write_sequential_length+=res->size;
				}
			}
			else{
				if(type==FS_GET_T){
					request_monitor.read_random_cnt++;
				}	
				else if(type==FS_SET_T){
					request_monitor.write_random_cnt++;
				}		
			}
		}


		switch(type){
			case FS_GET_T:
				temp->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
				measure_init(&temp->latency_checker);
				measure_start(&temp->latency_checker);
				break;
			case FS_SET_T:
#ifdef TRACE_REPLAY
				temp->value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
#else
				temp->value=inf_get_valueset(&res->buf[LPAGESIZE*i],FS_MALLOC_W,PAGESIZE);
#endif
				measure_init(&temp->latency_checker);
				measure_start(&temp->latency_checker);
				break;	
			case FS_FLUSH_T:
				break;
			case FS_DELETE_T:
				temp->value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
				break;
			default:
				printf("error type!\n");
				abort();
				break;
		}
		temp->key=creq->pos+i;
		if(print_read_latency && type==FS_GET_T){
			printf("READ: %u\n", temp->key);
		}

		if(prev_lba==UINT32_MAX){
			prev_lba=temp->key;
		}
		else{
			if(prev_lba+1==temp->key){
				consecutive_cnt++;
			}
			else{
				res->req_array[i-consecutive_cnt-1].is_sequential_start=(consecutive_cnt!=0);
				res->req_array[i-consecutive_cnt-1].consecutive_length=consecutive_cnt;
				consecutive_cnt=0;
			}
			prev_lba=temp->key;
			temp->consecutive_length=0;
		}

	//	printf("start REQ-TYPE:%s INFO(%d:%d) LBA: %u crc:%u\n", type_to_str(temp->type),creq->id, i, temp->key, temp->crc_value);
#ifdef TRACE_REPLAY
		if(temp->type==FS_SET_T){
			read(trace_fd, &CRCMAP[temp->key],sizeof(uint32_t));
			if(temp->key==test_key){
				printf("crc value:%u\n", CRCMAP[temp->key]);
			}
			*(uint32_t*)temp->value->value=CRCMAP[temp->key];
		}
		else{
			uint32_t temp_crc;
			read(trace_fd, &temp_crc, sizeof(uint32_t));
			temp->crc_value=CRCMAP[temp->key];
		}
#endif

#ifdef CHECKINGDATA
		if(temp->type==FS_SET_T){
			CRCMAP[temp->key]=crc32(&res->buf[LPAGESIZE*i],LPAGESIZE);	
		}
		else if(temp->type==FS_DELETE_T){
	//		CRCMAP[temp->key]=crc32(null_value, LPAGESIZE);
		}
		else if(temp->type==FS_GET_T){
			temp->crc_value=CRCMAP[temp->key];
		}
#endif


#ifdef TRACE_COLLECT
		if(temp->type==FS_SET_T){
			crc_buffer[i]=crc32(temp->value->value, LPAGESIZE);
		}
#endif
	}

	res->req_array[(res->size-1)-consecutive_cnt].is_sequential_start=(consecutive_cnt!=0);
	res->req_array[(res->size-1)-consecutive_cnt].consecutive_length=0;//consecutive_cnt;
	return res;
}

//extern int MS_TIME_SL;
//#define MS_TIME_SL 7
//set to time!!
//#define SLICE (32 * 1024)
vec_request **get_vectored_request_arr()
{
	static bool isstart = false;
	if (!isstart) {
		isstart = true;
		printf("now waiting req!!\n");
	}
	cheeze_ureq *ureq;
	vec_request **res = NULL;
	volatile uint8_t *send, *recv;
	bool check[CHEEZE_QUEUE_SIZE] = { 0, };
	int id;
	int check_idx = 0;
	int pre_i;
	uint32_t total_size = 0;

 retry:
	check_idx = 0;
	total_size = 0;
	memset(check, 0, sizeof(check));
	for (pre_i = 0; pre_i < CHEEZE_QUEUE_SIZE; pre_i++) {
		send = &send_event_addr[pre_i];
		if (*send) {
			id = pre_i;
			check[pre_i] = true;
			check_idx++;
			ureq = ureq_addr + id;
			total_size += ureq->len;
		}
	}
	if (!check_idx)
		goto retry;

	int req_idx = 0;
	static int cnt=0;
	static int previous_queue=0;
	uint32_t now_start_idx=previous_queue;
	bool check_continue=false;
	res = (vec_request **) malloc(sizeof(vec_request *) * (check_idx + 1));
	for (int i = now_start_idx, j=0; j < CHEEZE_QUEUE_SIZE; j++, i=(now_start_idx+j)%CHEEZE_QUEUE_SIZE) {
		if (!check[i]){
			
			if(check_continue==false){
				previous_queue=i;
				check_continue=true;
			}
//			printf("%u\n", i);
			continue;
		}
		send = &send_event_addr[i];
		recv = &recv_event_addr[i];
		id = i;
		ureq = ureq_addr + id;
		res[req_idx++] = ch_ureq2vec_req(ureq, id);
		if(cnt%100000){
			printf("%u\n", cnt++);
		}
#ifdef TRACE_COLLECT
		fdriver_lock(&trace_fd_lock);
		write(trace_fd, ureq, sizeof(cheeze_req_user));
		write(trace_fd, crc_buffer, ureq->len/LPAGESIZE * sizeof(uint32_t));
		fdriver_unlock(&trace_fd_lock);
#endif
		barrier();
		*send = 0;
	
#ifdef SYNC_WRITE
		if(ureq->op!=REQ_OP_READ && ureq->op!=REQ_OP_WRITE)
#else
		if (ureq->op!=REQ_OP_READ) 
#endif
		{
			barrier();
			*recv = 1;
		}
		if(i==CHEEZE_QUEUE_SIZE-1){
			previous_queue=0;
		}
	}
	res[req_idx] = NULL;
	return res;
}

extern void print_temp_log(int sig);

vec_request *get_trace_vectored_request(){
	static bool isstart=false;
	unsigned int crc_len;
	
	if(!isstart){
		isstart=true;
		printf("now waiting req!!\n");
		fsync(1);
		fsync(2);
	}

	cheeze_ureq ureq;
	vec_request *res=NULL;
	int id=0;
	static int cnt=1;
	while(1){
		uint32_t len=0;
		len=read(trace_fd, &ureq, sizeof(ureq));
#ifdef TRACE_REPLAY
		if(len !=0 && ureq.id==UINT32_MAX){
			print_temp_log(UINT32_MAX);
			continue;
		}
#endif
		if(len!=sizeof(ureq)){
			printf("read error!!: len:%u\n", len);
			raise(SIGINT);
			break;
			abort();
		}
		res=ch_ureq2vec_req(&ureq, id);
		cnt++;
		if(cnt%10000==0){
			printf("%u\n",cnt);
		}
		issue_req_num++;
		return res;
	}

	issue_req_num++;
	return res;
}

extern volatile vectored_request *now_processing;

bool cheeze_end_req(request *const req){
	vectored_request *preq=req->parents;
	//printf("req end_req:%u\n", req->key);
	uint32_t temp_crc;
	switch(req->type){
		case FS_NOTFOUND_T:
			bench_reap_data(req, mp.li, print_read_latency);
			//EPRINT("%u not found!\n", false, req->key);
#ifdef TRACE_REPLAY
			if(req->crc_value && req->crc_value!=*(uint32_t*)req->value->value){
				printf("not_found lba:%u data fail abort!\n", req->key);
				abort();
			}
#endif
			if(preq->buf){
				memcpy(&preq->buf[req->seq*LPAGESIZE], null_value,LPAGESIZE);
			}
			inf_free_valueset(req->value,FS_MALLOC_R);

			break;
		case FS_GET_T:
			bench_reap_data(req, mp.li, print_read_latency);
			if(req->value){

				if(preq->buf){
					memcpy(&preq->buf[req->seq*LPAGESIZE], req->value->value,LPAGESIZE);
				}
#ifdef TRACE_REPLAY
			if(req->crc_value!=*(uint32_t*)req->value->value){
				printf("lba:%u data fail abort! %u --> %u\n", req->key, req->crc_value, *(uint32_t*)req->value->value);
				abort();
			}
#endif

#ifdef CHECKINGDATA
				temp_crc=crc32(&preq->buf[req->seq*LPAGESIZE], LPAGESIZE);
				if(req->crc_value!=temp_crc){
					printf("\n");
					printf("\t\tfound - crc checking error in key:%u %u -> org:%u\n", req->key, temp_crc, CRCMAP[req->key]);	
					abort();
				}
				if(req->key==0){
					printf("target 0 crc:%u\n", CRCMAP[req->key]);
				}
#endif
				inf_free_valueset(req->value,FS_MALLOC_R);
			}
			break;
		case FS_SET_T:
			bench_reap_data(req, mp.li, print_read_latency);
			if(req->value) inf_free_valueset(req->value, FS_MALLOC_W);
			break;
		case FS_FLUSH_T:
		case FS_DELETE_T:
			inf_free_valueset(req->value, FS_MALLOC_W);
			break;
		default:
			abort();
	}

	pthread_mutex_lock(&req_cnt_lock);
	preq->done_cnt++;

#ifdef DEBUG
	cheeze_ureq *creq=(cheeze_ureq*)preq->origin_req;
#endif

	release_each_req(req);
	if(preq->size==preq->done_cnt){
		bench_vector_latency(preq);
		end_req_num++;

#ifdef WRITE_STOP_READ
		if(preq->type==FS_SET_T){
			fdriver_lock(&write_check_lock);
//			printf("return cnt:%u %u\n", write_cnt, preq->seq_id);
			write_cnt--;
			fdriver_unlock(&write_check_lock);
		}
#endif

#ifdef SYNC_WRITE
		if (preq->type==FS_SET_T) {
			recv_event_addr[preq->tag_id]=1;
		}
#endif

#ifdef TRACE_REPLAY

#else
		if(req->type==FS_GET_T || req->type==FS_NOTFOUND_T){
			recv_event_addr[preq->tag_id]=1;		
		}
#endif
		free(preq->req_array);
		now_processing=NULL;
		free(preq);
	}
	pthread_mutex_unlock(&req_cnt_lock);

	return true;
}

void free_cheeze(){
#ifdef CHECKINGDATA
	free(CRCMAP);
#endif
	return;
}

void free_trace_cheeze(){
#ifdef CHECKINGDATA
	free(CRCMAP);
#endif
	free(null_value);
	close(trace_fd);
	return;
}

void request_done_wait(){
	while(issue_req_num!=end_req_num){
		sleep(1);
	}
}
