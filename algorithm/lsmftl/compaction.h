#ifndef __COMPACTION_H__
#define __COMPACTION_H__
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../include/utils/tag_q.h"
#include "../../interface/queue.h"
#include "../../interface/interface.h"
#include "../../include/utils/thpool.h"
#include "../../include/sem_lock.h"
#include "key_value_pair.h"
#include "lftl_slab.h"
#include "level.h"
#include "sst_file.h"
#include "lsmtree.h"
#include "sst_block_file_stream.h"
#include <queue>

#define COMPACTION_TAGS ((QDEPTH)*2)

typedef struct inter_read_alreq_param{
	fdriver_lock_t done_lock;
	value_set *data;
	sst_file *target;
}inter_read_alreq_param;

typedef struct key_value_wrapper{ //for data read!
	uint32_t piece_ppa;
	key_value_pair kv_ptr;
	inter_read_alreq_param *param;
}key_value_wrapper;

typedef struct compaction_req{
	int8_t start_level;
	int8_t end_level;
	key_ptr_pair *target;
	void (*end_req)(struct compaction_req* req);
	void *param;
	uint32_t tag;
}compaction_req;

typedef struct compaction_master{
	queue *req_q;
	tag_manager *tm;
	pthread_t tid;
	slab_master *sm_comp_alreq;
	threadpool issue_worker;
	std::queue<inter_read_alreq_param*> *read_param_queue;
	inter_read_alreq_param *read_param;
	//slab_master *kv_wrapper_slab;
}compaction_master;


compaction_master *compaction_init(uint32_t compaction_queue_num);
void compaction_free(compaction_master *cm);
void compaction_issue_req(compaction_master *cm, compaction_req *);
level* compaction_first_leveling(compaction_master *cm, key_ptr_pair *, level *des);
level* compaction_leveling(compaction_master *cm, level *src, level *des);
level* compaction_tiering(compaction_master *cm, level *src, level *des);
level* compaction_merge(compaction_master *cm, level *tiered_level);

uint32_t compaction_read_param_remain_num(compaction_master *cm);
inter_read_alreq_param *compaction_get_read_param(compaction_master *cm);
void compaction_free_read_param(compaction_master *cm, inter_read_alreq_param *);
key_value_wrapper *compaction_get_kv_wrapper(uint32_t ppa);
void compaction_free_kv_wrapper(key_value_wrapper *kv_wrap);

static inline compaction_req * alloc_comp_req(int8_t start, int8_t end, key_ptr_pair *target,
		void (*end_req)(compaction_req *), void *param){
	compaction_req *res=(compaction_req*)malloc(sizeof(compaction_req));
	res->start_level=start; 
	res->end_level=end;
	res->target=target;
	res->end_req=end_req;
	res->param=param;
	return res;
}
#endif
