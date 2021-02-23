#ifndef __SST_SET_STREAM_H__
#define __SST_SET_STREAM_H__
#include "key_value_pair.h"
#include "../../interface/interface.h"
#include "compaction.h"
#include <queue>
enum {
SST_FILE_STREAM, KP_PAIR_STREAM
};

typedef struct{
	uint8_t type;
	bool (*check_done)(void *check_flag);
	std::queue<sst_file*> *sst_file_set;
	std::queue<void*> *check_flag_set;
	key_ptr_pair *kp_data;
	sst_file *now;
	uint32_t idx;
	bool full;
}sst_out_stream;

typedef struct{
	//std::queue<sst_file*> sst_file_set;
	sst_file *now;
	value_set *vs;
	uint32_t idx;
}sst_in_stream;

sst_out_stream* sst_os_init(sst_file *, inter_read_alreq_param *,
		uint32_t set_number, bool(*check_done)(void*));
sst_out_stream *sst_os_init_kp(key_ptr_pair *data);
void sst_os_add(sst_out_stream *os, sst_file *, 
		inter_read_alreq_param *, uint32_t num);
key_ptr_pair sst_os_pick(sst_out_stream *os);
void sst_os_pop(sst_out_stream *os);
void sst_os_free(sst_out_stream *os);
bool sst_os_is_empty(sst_out_stream *os);

sst_in_stream* sst_is_init();
void sst_is_set_space(sst_in_stream *is, value_set *data);
bool sst_is_insert(sst_in_stream *is, key_ptr_pair kp);

static inline value_set *sst_is_get_result(sst_in_stream *is, sst_file **result_ptr){
	value_set *res=is->vs;
	*result_ptr=is->now;
	is->vs=NULL;
	is->now=NULL;
	return res;
}

//char *sst_in_get_result(sst_file **result_ptr);
void sst_in_free(sst_in_stream *is);

#endif
