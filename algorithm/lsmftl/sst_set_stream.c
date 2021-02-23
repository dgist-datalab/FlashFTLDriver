#include "sst_set_stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

sst_out_stream* sst_os_init(sst_file *sst_set, inter_read_alreq_param *param, uint32_t set_num, bool(*check_done)(void*)){
	sst_out_stream *res=(sst_out_stream*)calloc(1, sizeof(sst_out_stream));
	res->type=SST_FILE_STREAM;
	res->sst_file_set=new std::queue<sst_file*>();
	res->check_flag_set=new std::queue<void*>();
	for(uint32_t i=0; i<set_num; i++){
		res->sst_file_set->push(&sst_set[i]);
		res->check_flag_set->push((void*)&param[i]);
	}
	res->now=NULL;
	res->idx=0;
	res->full=false;
	return res;
}

sst_out_stream *sst_os_init_kp(key_ptr_pair *data){
	sst_out_stream *res=(sst_out_stream*)calloc(1, sizeof(sst_out_stream));
	res->type=KP_PAIR_STREAM;
	res->kp_data=data;
	res->idx=0;
	return res;
}

void sst_os_add(sst_out_stream *os, sst_file *sst_set, inter_read_alreq_param *param, uint32_t set_num){
	for(uint32_t i=0; i<set_num; i++){
		os->sst_file_set->push(&sst_set[i]);
		os->check_flag_set->push((void*)&param[i]);
	}
}

key_ptr_pair sst_os_pick(sst_out_stream *os){
	if(os->type==KP_PAIR_STREAM){
		return os->kp_data[os->idx];
	}
retry:
	key_ptr_pair temp_res;
	if(os->full){
		if(os->sst_file_set->size()==0){
			temp_res.lba=UINT32_MAX;
			return temp_res;
		}

		os->now=os->sst_file_set->front();
		os->check_done(os->check_flag_set->front());

		os->sst_file_set->pop();
		os->check_flag_set->pop();

		os->full=false;
		os->idx=0;
	}


	if(os->now==NULL){
		os->now=os->sst_file_set->front();
		os->check_done(os->check_flag_set->front());

		os->sst_file_set->pop();
		os->check_flag_set->pop();
		os->full=false;
		os->idx=0;
	}

	key_ptr_pair res=*(key_ptr_pair*)&os->now->data[(os->idx)*sizeof(key_ptr_pair)];
	if(os->idx*sizeof(key_ptr_pair) >=PAGESIZE){
		os->full=true;
	}
	else if(res.lba==UINT32_MAX){
		os->full=true;
		goto retry;
	}
	return res;
}

void sst_os_pop(sst_out_stream *os){
	os->idx++;
	if(os->idx*sizeof(key_ptr_pair) >=PAGESIZE){
		os->full=true;
	}
}

void sst_os_free(sst_out_stream *os){
	delete os->sst_file_set;
	delete os->check_flag_set;
	free(os);
}

sst_in_stream* sst_is_init(){
	sst_in_stream *res=(sst_in_stream*)calloc(1,sizeof(sst_in_stream));
	res->now=NULL;
	return res;
}

bool sst_os_is_empty(sst_out_stream *os){
	return !os->full;
}

void sst_is_set_space(sst_in_stream *is, value_set *data){
	is->now=sst_init_empty();
	is->now->data=data->value;
	is->idx=0;
	is->vs=data;
}

bool sst_is_insert(sst_in_stream *is, key_ptr_pair kp){
	((key_ptr_pair*)is->vs->value)[is->idx++]=kp;
	if(is->idx * sizeof(key_ptr_pair)==PAGESIZE){
		return true;
	}
	return false;
}

void sst_is_free(sst_in_stream *is){
	free(is);
}
