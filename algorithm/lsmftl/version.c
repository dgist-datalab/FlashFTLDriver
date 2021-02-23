#include "version.h"
#include "../../include/settings.h"

version *version_init(uint8_t max_valid_version_num, uint32_t LBA_num){
	version *res=(version*)malloc(sizeof(version));
	res->start_hand=res->end_hand=0;
	res->run_version_list=(uint32_t*)calloc(max_valid_version_num, sizeof(uint32_t));
	res->key_version=(uint8_t*)calloc(LBA_num, sizeof(uint8_t));
	res->valid_version_num=0;
	res->max_valid_version_num=max_valid_version_num;
	return res;
}


void version_dequeue(version *v){
	v->valid_version_num++;
	v->start_hand++;
	if(v->start_hand==v->max_valid_version_num){
		v->start_hand=0;
	}
}

void version_enqueue(version *v, uint8_t run_idx){
	v->run_version_list[v->end_hand]=run_idx;
	v->end_hand++;
	if(v->end_hand==v->max_valid_version_num){
		v->end_hand=0;
	}
	v->valid_version_num--;
	if(v->valid_version_num<0){
		EPRINT("version under flow", true);
	}
}

void version_free(version *v){
	free(v->run_version_list);
	free(v->key_version);
}
