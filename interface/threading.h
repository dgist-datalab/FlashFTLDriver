#ifndef __H_THREADING__
#define __H_THREADING__
#include"interface.h"
#include"queue.h"
#include"../include/data_struct/redblack.h"
#include<pthread.h>
#include <queue>
typedef struct master_processor master_processor;
typedef struct processor{
	pthread_t t_id;
	pthread_t retry_id;

	pthread_mutex_t flag;
	master_processor *master;
	queue *req_q; //for write req in priority
	queue *retry_q;
#ifdef interface_pq
	queue *req_rq; //for read req
	Redblack qmanager;
	pthread_mutex_t qm_lock;
#endif
	volatile bool retry_stop_flag;
	pthread_mutex_t read_retry_lock;
	pthread_cond_t read_retry_cond;
	std::queue <void *>*read_retry_q;
}processor;

struct master_processor{
	bool _data_check_flag;
	bool data_load;
	bool data_dump;
	processor *processors;
	pthread_mutex_t flag;
	lower_info *li;
	algorithm *algo;
	blockmanager *bm;
	bool stopflag;
};
#endif
