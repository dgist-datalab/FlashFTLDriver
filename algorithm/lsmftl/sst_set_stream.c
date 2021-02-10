#include "sst_set_stream.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

sst_out_stream* sst_os_init(sst_file *sst_set, comp_read_alreq_params *params, uint32_t set_number, bool(*check_done)(void*)){
	sst_out_stream *res=(sst_out_stream*)calloc(1, sizeof(sst_out_stream));
	res->type=KP_FILE_STREAM;
	res->sst_file_set=new std::queue<sst_file*>();
	res->check_flag_set=new std::queue<void*>();
	for(uint32_t i=0; i<set_num; i++){
		res->sst_file_set->push(&sst_set[i]);
		res->check_flag_set->push((void*)&params[i]);
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

void sst_os_add(sst_out_stream *os, sst_file *sst_set, comp_read_alreq_params *params, uint32_t num){
	for(uint32_t i=0; i<set_num; i++){
		os->sst_file_set->push(&sst_set[i]);
		os->check_flag_set->push((void*)&params[i]);
	}
}

key_ptr_pair sst_os_pick(sst_out_stream *os){
	if(os->type==KP_PAIR_STREAM){
		return os->kp_data[os->idx];
	}
retry:
	if(os->full){
		if(os->sst_file_set->size()==0){
			return UINT32_MAX;
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

sst_is_stream* sst_is_init(char*(*get_resut)()){
	sst_in_stream *res=(sst_is_stream)calloc(1,sizeof(sst_in_stream));
	res->now=NULL;
}

void sst_is_set_space(sst_in_strea *is, value_set *data){
	is->now=sst_init_empty();
	is->now->data=(void*)data->value;
	is->idx=0;
	is->vs=data;
}

value_set *sst_is_insert(sst_in_stream *is, key_ptr_pair kp, sst_file **result_ptr){
	((key_ptr_pair*)is->data)[is->idx++]=kp;
	if(is->idx * sizeof(key_ptr_pair)==PAGESIZE){
		value_set *res=is->vs;
		*result_ptr=is->now;
		is->vs=NULL;
		is->now=NULL;
		return res;
	}
	return NULL;
}

void sst_is_free(sst_is_stream *is){
	free(is);
}
