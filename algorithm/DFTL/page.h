#include "../../include/settings.h"
#include "../../include/container.h"
typedef struct page_params{
	request *parents;
	uint32_t address;
	value_set *value;
}page_params;

typedef struct align_buffer{
	uint8_t idx;
	KEYT key[L2PGAP];
	char *value;
}align_buffer;

uint32_t page_create (lower_info*,blockmanager *, algorithm *);
void page_destroy (lower_info*,  algorithm *);
uint32_t page_argument(int argc, char **argv);
uint32_t page_read(request *const);
uint32_t page_write(request *const);
uint32_t page_remove(request *const);
uint32_t page_flush(request *const);
void *page_end_req(algo_req*);

extern struct algorithm demand_ftl;
inline void send_user_req(request *const req, uint32_t type, ppa_t ppa,value_set *value){
	/*you can implement your own structur for your specific FTL*/
	page_params* params=(page_params*)malloc(sizeof(page_params));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	params->value=value;
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->param=(void*)params;//add your parameter structure 
	my_req->type=type;//DATAR means DATA reads, this affect traffics results
	/*you note that after read a PPA, the callback function called*/

	switch(type){
		case DATAR:
			demand_ftl.li->read(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
		case DATAW:
			demand_ftl.li->write(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
	}
}
