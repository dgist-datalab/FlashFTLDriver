#ifndef FLASHFTLDRIVER_BLOOMFTL_HYBRID_H
#define FLASHFTLDRIVER_BLOOMFTL_HYBRID_H

#include "../../include/settings.h"
#include "../../include/container.h"
#include <map>

typedef struct hybrid_param {
	request* parents;
	uint32_t address;
	value_set* value;
}hybrid_param;

typedef struct align_buffer {
	uint8_t idx;
	value_set* value[L2PGAP];
	KEYT key[L2PGAP];
}align_buffer;


typedef struct hybrid_read_buffer {
	std::multimap<uint32_t, algo_req*>* pending_req;
	std::multimap<uint32_t, algo_req*>* issue_req;
	fdriver_lock_t pending_lock;
	fdriver_lock_t read_buffer_lock;
	uint32_t buffer_ppa;
	char buffer_value[PAGESIZE];
}hybrid_read_buffer;



uint32_t hybrid_argument(int argc, char **argv);
uint32_t  hybrid_create(lower_info*, blockmanager*, algorithm*);
void  hybrid_destroy(lower_info*, algorithm*);
uint32_t hybrid_flush(request*const);
uint32_t hybrid_remove(request*const);
uint32_t hybrid_read(request* const);
uint32_t hybrid_write(request* const);
void *hybrid_end_req(algo_req*);


#endif //FLASHFTLDRIVER_BLOOMFTL_HYBRID_H
