#ifndef P_BBUF_W_H
#define P_BBUF_W_H
//SEG 7bit(a), 7bit(b)
//SEG 7bit(b) * R2PGAP =>8bit original PPB,  7bit(a)
#include <stdlib.h>
#include <stdio.h>
#include <list>
#include <pthread.h>
#include "./amf_info.h"
#include "../../include/utils/tag_q.h"
#include "../../include/sem_lock.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"

#define BUF_IDX(ppa)  ((ppa /_PPB) & (AMF_PUNIT-1))
#define REAL_PPA(ppa, offset) ((ppa & (_PPB-1))*R2PGAP+offset)

typedef struct return_param{
	bool sync;
	uint32_t cnt;
	uint32_t tag;
	fdriver_lock_t lock;
}return_param;

typedef struct algo_req_wrapper{
	uint32_t type;
	uint32_t rppa;
	uint32_t target;
	return_param *rp;
	char *data;
	algo_req *org_req;
	void *org_param;
	void *(*org_end_req)(algo_req *req);
	uint32_t tag_num;
	struct physical_block_buffer *buf;
}algo_req_wrapper;


typedef struct physical_block_buffer{
	uint32_t idx;
	fdriver_lock_t idle_check_lock;
	fdriver_lock_t schedule_lock;
	algo_req_wrapper req[RPPB];
	return_param return_param_array[_PPB];

	std::list<uint32_t> schedule_queue;
	tag_manager *rp_tag;
	tag_manager *tag;
}physical_block_buffer; //p_bbuf;

void p_bbuf_init();
void p_bbuf_issue(uint32_t type, uint32_t ppa, char *data, 
		algo_req *req);
void p_bbuf_sync_issue(uint32_t type, uint32_t ppa, char *data);
void p_bbuf_join();
void p_bbuf_free();
#endif
