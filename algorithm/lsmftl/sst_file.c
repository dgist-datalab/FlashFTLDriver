#include "sst_file.h"
#include <stdlib.h>

sst_file *sst_init_empty(){
	return (sst_file*)calloc(1,sizeof(sst_file));
}

sst_file *sst_init(uint32_t ppa, uint32_t start_lba, uint32_t end_lba){
	sst_file *res=(sst_file*)calloc(1,sizeof(sst_file));
	res->ppa=ppa;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	return res;
}
