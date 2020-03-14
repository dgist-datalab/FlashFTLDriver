#include "../../include/container.h"
typedef struct page_params{
	request *parents;
	int test;
}page_params;

uint32_t page_create (lower_info*,blockmanager *, algorithm *);
void page_destroy (lower_info*,  algorithm *);
uint32_t page_read(request *const);
uint32_t page_write(request *const);
uint32_t page_remove(request *const);
void *page_end_req(algo_req*);
