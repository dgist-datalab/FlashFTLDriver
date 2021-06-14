#include "cheeze_hg_block.h"
#include "queue.h"
#include "interface.h"
#include "threading.h"
#include "../bench/bench.h"
#include "vectored_interface.h"
#include "../include/utils/crc32.h"
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
static int trace_fd = 0;
static uint32_t trace_crc[TRACE_DEV_SIZE/LPAGESIZE];
static uint32_t trace_crc_buf[CRC_BUFSIZE];

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

#ifdef CHECKINGDATA
uint32_t* CRCMAP;
#endif
#define TRACE_TARGET "/trace"
void init_trace_cheeze(){
	trace_fd = open(TRACE_TARGET, O_RDONLY);
	if (trace_fd < 0) {
		perror("Failed to open " TRACE_TARGET);
		abort();
		return;
	}

	null_value = (char*)malloc(PAGESIZE);
	memset(null_value, 0, PAGESIZE);
}

void init_cheeze(uint64_t phy_addr){
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

#ifdef CHECKINGDATA
	CRCMAP=(uint32_t*)malloc(sizeof(uint32_t) * RANGE);
	memset(CRCMAP, 0, sizeof(uint32_t) *RANGE);
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
	vec_request *res=(vec_request *)calloc(1, sizeof(vec_request));
	res->tag_id=id;

	error_check(creq);

	res->origin_req=(void*)creq;
	res->size=creq->len/LPAGESIZE;
	res->req_array=(request*)calloc(res->size,sizeof(request));
	res->end_req=NULL;
	res->mark=0;

	FSTYPE type=decode_type(creq->op);
	if(type!=FS_GET_T && type!=FS_SET_T){
		res->buf=NULL;
	}
	else{
		if(trace_fd){
			res->buf=NULL;	
		}
		else{
			res->buf=get_buf_addr(data_addr, id);
		}
	}


	if(res->size > QSIZE){
		printf("----------------number of reqs is over %u < %u\n", QSIZE, res->size);
		abort();
		return NULL;
	}

	for(uint32_t i=0; i<res->size; i++){
		request *temp=&res->req_array[i];
		temp->parents=res;
		temp->type=type;
		temp->end_req=cheeze_end_req;
		temp->isAsync=ASYNC;
		temp->seq=i;
		switch(type){
			case FS_GET_T:
				temp->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
				if(trace_fd)
					memcpy(&temp->crc_value, trace_crc_buf + i, sizeof(uint32_t));
				break;
			case FS_SET_T:
				if(trace_fd){
					/*crc map*/
					//memcpy(trace_crc + (creq->pos + i), trace_crc_buf + i, sizeof(uint32_t));
					temp->value=inf_get_valueset(NULL, FS_MALLOC_W, LPAGESIZE);
					memcpy(&temp->crc_value, trace_crc_buf + i, sizeof(uint32_t));
					memcpy(temp->value->value, trace_crc_buf + i, sizeof(uint32_t));
				}else{
					temp->value=inf_get_valueset(&res->buf[LPAGESIZE*i],FS_MALLOC_W,LPAGESIZE);
				}
				break;	
			case FS_FLUSH_T:
				break;
			case FS_DELETE_T:
				/*clear crc map*/
				if (trace_fd)
					memset(trace_crc + (creq->pos + i), 0, sizeof(uint32_t));
				break;
			default:
				printf("error type!\n");
				abort();
				break;
		}
		temp->key=creq->pos+i;
		if(temp->key==28895274){	
			printf("start REQ-TYPE:%s INFO(%d:%d) LBA: %u crc:%u\n", type_to_str(temp->type),creq->id, i, temp->key, temp->crc_value);
		}

#ifdef CHECKINGDATA
		if(temp->type==FS_SET_T){
			CRCMAP[temp->key]=crc32(&res->buf[LPAGESIZE*i],LPAGESIZE);	
		}
		else if(temp->type==FS_DELETE_T){
			CRCMAP[temp->key]=crc32(null_value, LPAGESIZE);
		}
#endif
		DPRINTF("REQ-TYPE:%s INFO(%d:%d) LBA: %u\n", type_to_str(temp->type),creq->id, i, temp->key);
	}
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

#if 0
	uint32_t tmp = total_size / SLICE * MS_TIME_SL;
	if (!tmp)
		tmp = 1;
	static int debug_cnt = 0;
	if (++debug_cnt % 100000 == 0) {
		printf("tmp:%u\n", tmp);
	}
	usleep(tmp);
#endif

	int req_idx = 0;
	res = (vec_request **) malloc(sizeof(vec_request *) * (check_idx + 1));
	for (int i = 0; i < CHEEZE_QUEUE_SIZE; i++) {
		if (!check[i])
			continue;
		send = &send_event_addr[i];
		recv = &recv_event_addr[i];
		id = i;
		ureq = ureq_addr + id;
		res[req_idx++] = ch_ureq2vec_req(ureq, id);
		barrier();
		*send = 0;
		if (ureq->op!=REQ_OP_READ) {
			barrier();
			*recv = 1;
		}
	}
	res[req_idx] = NULL;
	return res;
}

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
		if(len!=sizeof(ureq)){
			printf("read error!!: len:%u\n", len);
			abort();
		}
		crc_len = ureq.len/LPAGESIZE * sizeof(uint32_t);
		len=read(trace_fd, trace_crc_buf, crc_len);
		if(len!=crc_len){
			printf("read error!!: len:%u, crc_len:%u\n", len, crc_len);
			abort();
		}
		res=ch_ureq2vec_req(&ureq, id);
		cnt++;
		if(cnt%10000==0){
			printf("%u\n",cnt);
		}
		return res;
	}

	return res;
}

bool cheeze_end_req(request *const req){
	vectored_request *preq=req->parents;
	if(req->key==28895274){	
		printf("end REQ-TYPE:%s INFO(%d:%d) LBA: %u req->crc:%u\n", type_to_str(req->type), 0,0, req->key, req->crc_value);
	}
	switch(req->type){
		case FS_NOTFOUND_T:

			/*crc map check for not found check!! by crc*/
			if (trace_fd && (trace_crc[req->key] != 0) ) {
				printf("[ERROR] NOTFOUND - key:%u, origin crc: %u\n", req->key, trace_crc[req->key]);	
				abort();
			}
			bench_reap_data(req, mp.li);
			DPRINTF("%u not found!\n",req->key);
#ifdef CHECKINGDATA
			if(CRCMAP[req->key]){
				printf("\n");
				printf("\t\tcrc checking error in key:%u\n", req->key);	
				printf("\n");		
			}
#endif
			if(preq->buf){
				memcpy(&preq->buf[req->seq*LPAGESIZE], null_value,LPAGESIZE);
			}
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FS_GET_T:
			/*crc map check!! by crc*/
			if(trace_fd && req->key==28895274){
				printf("%u crc value:%u\n", req->key, *(uint32_t*)req->value->value);
			}
			if (trace_fd && trace_crc[req->key] != 0) {
				uint32_t crc_result = *((uint32_t *)req->value->value);
				if (crc_result != trace_crc[req->key]) {
					printf("[ERROR] FOUND - key:%u, origin crc: %u, result crc: %u\n", req->key, trace_crc[req->key], crc_result);
					abort();
				}
			}
			bench_reap_data(req, mp.li);
			if(req->value){

				if(preq->buf){
					memcpy(&preq->buf[req->seq*LPAGESIZE], req->value->value,LPAGESIZE);
				}
#ifdef CHECKINGDATA
				if(CRCMAP[req->key]!=crc32(&preq->buf[req->seq*LPAGESIZE], LPAGESIZE)){
					printf("\n");
					printf("\t\tcrc checking error in key:%u\n", req->key);	
					printf("\n");
				}	
#endif
				inf_free_valueset(req->value,FS_MALLOC_R);
			}
			break;
		case FS_SET_T:
			//memcpy(trace_crc + req->key, req->crc, sizeof(uint32_t));
			if (trace_fd)
				trace_crc[req->key] = req->crc_value;
			bench_reap_data(req, mp.li);
			if(req->value) inf_free_valueset(req->value, FS_MALLOC_W);
			break;
		case FS_FLUSH_T:
		case FS_DELETE_T:
			break;
		default:
			abort();
	}
	preq->done_cnt++;
	release_each_req(req);

	if(preq->size==preq->done_cnt){
		if(!trace_fd){
			if(req->type==FS_GET_T || req->type==FS_NOTFOUND_T){
				recv_event_addr[preq->tag_id]=1;		
			}
		}
		free(preq->req_array);
		free(preq);
	}

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

