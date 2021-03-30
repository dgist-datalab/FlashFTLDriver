#include "version.h"
#include "../../include/settings.h"
#include <math.h>

version *version_init(uint8_t max_valid_version_num, uint32_t LBA_num){
	version *res=(version*)malloc(sizeof(version));
	res->start_hand=res->end_hand=0;
	res->key_version=(uint8_t*)calloc(LBA_num, sizeof(uint8_t));
	res->valid_version_num=0;
	res->max_valid_version_num=max_valid_version_num;

	res->ridx_empty_queue=new std::queue<uint32_t>();
	printf("max_version idx:%u\n", max_valid_version_num-1);
	for(uint32_t i=0; i<max_valid_version_num; i++){
		res->ridx_empty_queue->push(i);
	}

	res->ridx_populate_queue=new std::queue<uint32_t>();
	res->memory_usage_bit=ceil(log2(max_valid_version_num))*LBA_num;
	res->poped_version_num=0;
	return res;
}

uint32_t version_get_empty_ridx(version *v){
	if(v->ridx_empty_queue->empty()){
		EPRINT("should merge before empty ridx", true);
	}
	uint32_t res=v->ridx_empty_queue->front();
	v->ridx_empty_queue->pop();
	return res;
}

void version_get_merge_target(version *v, uint32_t *ridx_set){
	for(uint32_t i=0; i<2; i++){
		ridx_set[i]=v->ridx_populate_queue->front();
		v->ridx_populate_queue->pop();
	}
}

void version_unpopulate_run(version *v, uint32_t ridx){
	v->ridx_empty_queue->push(ridx);
}

void version_populate_run(version *v, uint32_t ridx){
	v->ridx_populate_queue->push(ridx);
}

void version_sanity_checker(version *v){
	uint32_t remain_empty_size=v->ridx_empty_queue->size();
	uint32_t populate_size=v->ridx_populate_queue->size();
	if(remain_empty_size+populate_size!=v->max_valid_version_num){
		printf("error log : empty-size(%d) populate-size(%d)\n", remain_empty_size, populate_size);
		EPRINT("version sanity error", true);
	}
}

void version_free(version *v){
	delete v->ridx_empty_queue;
	delete v->ridx_populate_queue;
	free(v->key_version);
	free(v);
}
extern uint32_t debug_lba;
void version_coupling_lba_ridx(version *v, uint32_t lba, uint8_t ridx){
	if(ridx>TOTALRUNIDX){
		EPRINT("over version num", true);
	}
	if(lba==debug_lba){
		printf("[version_map] lba:%u->%u\n",lba, ridx);
	}
	v->key_version[lba]=ridx;
}
