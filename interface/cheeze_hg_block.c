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

static void *page_addr;
static uint8_t *send_event_addr; // CHEEZE_QUEUE_SIZE ==> 16B
static uint8_t *recv_event_addr; // 16B
static uint64_t *seq_addr; // 8KB
struct cheeze_req_user *ureq_addr; // sizeof(req) * 1024
static char *data_addr[2]; // page_addr[1]: 1GB, page_addr[2]: 1GB
static uint64_t seq = 0;

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

void init_cheeze(){
	int chrfd = open("/dev/mem", O_RDWR);
	if (chrfd < 0) {
		perror("Failed to open /dev/mem");
		abort();
		return;
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
		res->buf=get_buf_addr(data_addr, id);
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
				break;
			case FS_SET_T:
				temp->value=inf_get_valueset(&res->buf[LPAGESIZE*i],FS_MALLOC_W,LPAGESIZE);
				break;	
			case FS_FLUSH_T:
				break;
			case FS_DELETE_T:
				break;
			default:
				printf("error type!\n");
				abort();
				break;
		}
		temp->key=creq->pos+i;

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

vec_request *get_vectored_request(){
	static bool isstart=false;
	
	if(!isstart){
		isstart=true;
		printf("now waiting req!!\n");
	}

	cheeze_ureq *ureq;
	vec_request *res=NULL;
	uint8_t *send, *recv;
	int id;
	while(1){
		for (int i = 0; i < CHEEZE_QUEUE_SIZE; i++) {
			send = &send_event_addr[i];
			recv = &recv_event_addr[i];
			if (*send) {
				id = i;
				// printf("id: %d (i: %d, j: %d), seq_addr[id]: %lu, seq: %lu\n", id, i, j, seq_addr[id], seq);
				// ureq_print(ureq);
				if (seq_addr[id] == seq) {
					ureq = ureq_addr + id; 
					res=ch_ureq2vec_req(ureq, id);
					*send = 0;
					if(ureq->op!=REQ_OP_READ){
						*recv = 1;
					}
					seq++;
					return res;
				} else {
					continue;
				}
			}
		}
	}

	return res;
}

bool cheeze_end_req(request *const req){
	vectored_request *preq=req->parents;
	switch(req->type){
		case FS_NOTFOUND_T:
			bench_reap_data(req, mp.li);
			DPRINTF("%u not found!\n",req->key);
#ifdef CHECKINGDATA
			if(CRCMAP[req->key]){
				printf("\n");
				printf("\t\tcrc checking error in key:%u\n", req->key);	
				printf("\n");		
			}
#endif
			memcpy(&preq->buf[req->seq*LPAGESIZE], null_value,LPAGESIZE);
			inf_free_valueset(req->value,FS_MALLOC_R);
			break;
		case FS_GET_T:
			bench_reap_data(req, mp.li);
			if(req->value){
				memcpy(&preq->buf[req->seq*LPAGESIZE], req->value->value,LPAGESIZE);
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
		if(req->type==FS_GET_T && req->type==FS_NOTFOUND_T){
			recv_event_addr[preq->tag_id]=1;		
		}
		free(preq->origin_req);
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
