#ifndef __SST_H__
#define __SST_H__
#include "read_helper.h"
#include "key_value_pair.h"
#include <stdint.h>
typedef struct sst_file{
	uint32_t ppa;
	uint32_t start_lba;
	uint32_t end_lba;
	read_helper *_read_helper;
	char *data;
}sst_file;

sst_file *sst_init_empty();
sst_file *sst_init(uint32_t ppa, uint32_t start_lba, uint32_t end_lba);

static inline void sst_deep_copy(sst_file *des, sst_file *src){
	read_helper *temp_helper=des->_read_helper;
	*des=*src;
	//read_helper_copy(temp_helper, src->_read_helper);
	des->_read_helper=temp_helper;
}

static inline void sst_copy(sst_file *des, sst_file *src){
	*des=*src;
}

#define for_each_kp(data_ptr, kp_ptr, kp_idx)\
	for(kp_idx=0, kp_ptr=(key_ptr_pair*)&data_ptr[kp_idx*sizeof(key_ptr_pair)];\
			(kp_idx*sizeof(key_ptr_pair) < PAGESIZE && kp_ptr->lba!=UINT32_MAX);\
			kp_idx++, kp_ptr=(key_ptr_pair*)&data_ptr[kp_idx*sizeof(key_ptr_pair)])
#endif
