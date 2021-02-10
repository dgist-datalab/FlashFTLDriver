#ifndef __SST_SET_STREAM_H__
#define __SST_SET_STREAM_H__
#include "key_value_pair.h"
#include "../../interface/interface.h"
#include "compaction.h"
#include <queue>
typedef struct{
	bool (*check_done)(void *check_flag);
	std::queue<sst_file*> *sst_file_set;
	std::queue<void*> *check_flag_set;
	sst_file *now;
	uint32_t idx;
	bool now_full;
}sst_out_stream;

typedef struct{
	//std::queue<sst_file*> sst_file_set;
	sst_file *now;
	value_set *vs;
	uint32_t idx
}sst_in_stream;

sst_out_stream* sst_os_init(sst_file *, comp_read_alreq_params *,
		uint32_t set_number, bool(*check_done)(void*));
void sst_os_add(sst_out_stream *os, sst_file *, 
		comp_read_alreq_params *, uint32_t num);
key_ptr_pair sst_os_pick(sst_out_stream *os);
key_ptr_pair sst_os_pop(sst_out_stream *os);
void sst_os_free(sst_out_stream *os);
bool sst_os_is_empty(sst_out_stream *os);

sst_in_stream* sst_is_init();
void sst_is_set_space(sst_in_strea *is, value_set *data);
value_set *sst_is_insert(sst_in_stream *is, key_ptr_pair kp, sst_file **result_ptr);
//char *sst_in_get_result(sst_file **result_ptr);
void sst_in_free(sst_is_stream *is);

#endif
