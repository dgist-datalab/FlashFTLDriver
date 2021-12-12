#ifndef N_WRITE_H
#define N_WRITE_H
#include "./amf_info.h"
#include "../../include/sem_lock.h"
typedef struct dummy_req{
	uint32_t test_ppa;
	uint8_t type;
}dummy_req;

typedef struct amf_wrapper{
	bool sync;
	uint32_t cnt;
	uint32_t ppa;
	algo_req *req;
	fdriver_lock_t lock;
}amf_wrapper;

void normal_write_init();
void normal_write_issue(uint32_t type, uint32_t ppa, char *data, 
		algo_req *req);
void normal_write_sync_issue(uint32_t type, uint32_t ppa, char *data);
void normal_write_free();
#endif
