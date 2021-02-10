#ifndef __SST_H__
#define __SST_H__
#include "read_helper.h"
typedef struct sst_file{
	uint32_t piece_ppa;
	uint32_t start_lba;
	read_helper *_read_helper;
	void *data;
}sst_file;

sst_file *sst_init_empty();
sst_file *sst_init(uint32_t piece_ppa, uint32_t start_lba);

static inline void sst_deep_copy(sst_file *des, sst_file *src){
	read_helper *temp_helper=des->_read_helper;
	*des=*src;
	read_helper_copy(temp_helper, src->_read_helper);
	des->_read_heleper=temp_helper;
}

static inline void sst_copy(sst_file *des, sst_file *src){
	*des=*src;
}
#endif
