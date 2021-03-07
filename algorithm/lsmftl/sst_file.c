#include "sst_file.h"
#include <stdlib.h>

sst_file *sst_init_empty(uint8_t type){
	sst_file *res=(sst_file*)calloc(1,sizeof(sst_file));
	res->type=type;
	return res;
}

sst_file *sst_pf_init(uint32_t ppa, uint32_t start_lba, uint32_t end_lba){
	sst_file *res=(sst_file*)calloc(1,sizeof(sst_file));
	res->file_addr.map_ppa=ppa;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->type=PAGE_FILE;
	return res;
}

sst_file *sst_bf_init(uint32_t ppa, uint32_t end_ppa, uint32_t start_lba, uint32_t end_lba){
	sst_file *res=(sst_file*)calloc(1,sizeof(sst_file));
	res->file_addr.piece_ppa=ppa;
	res->end_ppa=end_ppa;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->type=BLOCK_FILE;
	return res;
}

void sst_destroy_content(sst_file* sstfile){
	//free_sstfile_ readhelper...
	return;
}

void sst_free(sst_file* sstfile){
	//free_sstfile
	if(sstfile->data){
		EPRINT("data not free", true);
	}
	free(sstfile);

}
