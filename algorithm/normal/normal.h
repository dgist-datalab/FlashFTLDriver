#include "../../include/container.h"
#include "../../include/settings.h"
#define MAXPHYSICALADDR (TOTALSIZE/PAGESIZE)

typedef struct normal_params{
	request *parents;
	int test;
}normal_params;

uint32_t normal_create (lower_info*, blockmanager * d,algorithm *);
void normal_destroy (lower_info*,  algorithm *);
uint32_t normal_read(request *const);
uint32_t normal_write(request *const);
uint32_t normal_remove(request *const);
void *normal_end_req(algo_req*);
