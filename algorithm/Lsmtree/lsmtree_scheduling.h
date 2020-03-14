#include <stdio.h>
#include <pthread.h>
#include "../../include/settings.h"
#include "../../include/container.h"
#include "../../interface/queue.h"
#include "lsmtree.h"

#define SCHED_FLUSH 0
#define SCHED_HWRITE 1
#define SCHED_HREAD 2

typedef struct sched_node{
	uint8_t type;
	void *param;
}sched_node;

typedef struct lsm_io_sched{
	pthread_mutex_t sched_lock;
	pthread_cond_t sched_cond;
	queue *q;
	volatile bool run_flag;
}lsm_io_scheduler;

void lsm_io_sched_init();
void lsm_io_sched_push(uint8_t type,void *req);
void lsm_io_sched_flush();
void lsm_io_sched_finish();
