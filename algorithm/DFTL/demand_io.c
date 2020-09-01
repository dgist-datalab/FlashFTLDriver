#include "demand_io.h"
#include "../../interface/interface.h"
#include <stdlib.h>

void *demend_end_req(algo_req * my_req){
	request *parents=my_req->parents;
	switch(my_req->type){
		case MAPPINGR:
		case MAPPINGW:
			inf_assign_try(parents);
			break;
		default:
			printf("unknown type :%d (%s:%d)\n",my_req->type, __FILE__, __LINE__);
			abort();
			break;
	}
	free(my_req);
	return NULL;
}

void *demend_inter_end_req(algo_req *my_req){
	gc_map_value *gmv=(gc_map_value*)my_req->params;
	switch(my_req->type){
		case GCMR_DGC:
			gmv->isdone=true;
			break;
		case GCMW_DGC:
			inf_free_valueset(gmv->value, FS_MALLOC_R);
			free(gmv);
			break;
	}
	free(my_req);
	return NULL;
}

void demand_mapping_read(uint32_t ppa, lower_info *li, request *req, void *params){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->type=MAPPINGR;
	my_req->parents=req;
	my_req->type_lower=0;
	my_req->end_req=demend_end_req;
	my_req->params=params;
	my_req->ppa=ppa;
	li->read(ppa, PAGESIZE, req->value, ASYNC, my_req);
}

void demand_mapping_write(uint32_t ppa, lower_info *li, request *req, void *params){
	if(ppa==667793){
		printf("break!\n");
	}
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->type=MAPPINGW;
	my_req->parents=req;
	my_req->type_lower=0;
	my_req->end_req=demend_end_req;
	my_req->params=params;
	my_req->ppa=ppa;
	li->write(ppa, PAGESIZE, req->value, ASYNC, my_req);
}

void demand_mapping_inter_read(uint32_t ppa, lower_info *li, gc_map_value *params){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->type=GCMR_DGC;
	my_req->parents=NULL;
	my_req->type_lower=0;
	my_req->end_req=demend_inter_end_req;
	my_req->params=(void *)params;
	my_req->ppa=ppa;

	li->read(ppa, PAGESIZE, params->value, ASYNC, my_req);
}

void demand_mapping_inter_write(uint32_t ppa, lower_info *li, gc_map_value *params){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->type=GCMW_DGC;
	my_req->parents=NULL;
	my_req->type_lower=0;
	my_req->end_req=demend_inter_end_req;
	my_req->params=(void *)params;
	my_req->ppa=ppa;

	li->write(ppa, PAGESIZE, params->value, ASYNC, my_req);
}
