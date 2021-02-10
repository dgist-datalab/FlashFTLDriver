#ifndef __COMPACTION_H__
#define __COMPACTION_H__
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../include/utils/tag_q.h"
#include "../../interface/queue.h"
#include "../../include/thpool.h"
#include "lftl_slab.h"
#include "sst_file.h"

#define COMPACTION_TAGS 64

typedef struct{
	fdriver_lock_t done_lock;
	value_set *data;
	sst_file *target;
}comp_read_alreq_params;

typedef struct{
	int8_t start_level;
	int8_t end_level;
	key_ptr_pair *target;
	void (*end_req)(void* req);
	void *params;
	uint32_t tag;
}compaction_req;

typedef struct {
	queue *req_q;
	tag_manager *tm;
	pthread_t tid;
	slab_master *sm_comp_alreq;
	threadpool issue_worker;
	comp_alreq_params *read_params;
}compaction_master;

compaction_master *compaction_init(uint32_t compaction_queue_num);
void compaction_free();
void compaction_issue_req(compaction_req *);
uint32_t compaction_first_leveling(compaction_master *cm, key_ptr_pair *, level *des);
uint32_t compaction_leveling(compaction_master *cm, level *src, level *des);
uint32_t compaction_tiering(compaction_master *cm, level *src, level *des);
uint32_t compaction_merge(compaction_master *cm, run *r1, run* r2, uint8_t version_idx);

static inline compaction_req * alloc_comp_req(int8_t start, int8_t end, key_ptr_pair *target,
		void (*end_req)(void*), void *params){
	compaction_req *res=(compaction_req*)malloc(sizeof(compaction_req));
	res->start=start; 
	res->end=end;
	res->target=target;
	res->end_req=end_req;
	res->params=params;
	return res;
}
#endif
