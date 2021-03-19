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

void sst_destroy_content(sst_file* sstfile, page_manager *pm){
	//free_sstfile_ readhelper...
	
	if(sstfile->type==BLOCK_FILE){
		for(uint32_t i=0; i<sstfile->map_num; i++){
			invalidate_map_ppa(pm->bm, sstfile->block_file_map[i].ppa);
		}
		free(sstfile->block_file_map);
	}
	if(sstfile->data){
		EPRINT("data not free", true);
	}
	if(sstfile->_read_helper){
		read_helper_free(sstfile->_read_helper);
	}

	return;
}

void sst_set_file_map(sst_file *sstfile, uint32_t map_num, map_range *map_range){
	if(sstfile->type!=BLOCK_FILE){
		EPRINT("cannot have map!", true);
	}
	sstfile->block_file_map=map_range;
	/*
	for(uint32_t i=0; i<map_num; i++){
		printf("[%p]s:%u e:%u p:%u\n", sstfile,
				sstfile->block_file_map[i].start_lba,
				sstfile->block_file_map[i].end_lba,
				sstfile->block_file_map[i].ppa);
	}*/
}

uint32_t sst_find_map_addr(sst_file *sstfile, uint32_t lba){
	if(sstfile->type!=BLOCK_FILE){
		EPRINT("cannot have map!", true);
	}
	uint32_t res=UINT32_MAX;

	int s=0, e=sstfile->map_num;
	while(s<=e){
		int mid=(s+e)/2;
		map_range *mr=&sstfile->block_file_map[mid];
		if(mr->start_lba<=lba && mr->end_lba>=lba){
			res=mr->ppa;
			break;
		}
		if(mr->start_lba > lba){
			e=mid-1;
		}
		else if(mr->end_lba<lba){
			s=mid+1;
		}
	}

	return res;
}

void sst_free(sst_file* sstfile, page_manager *pm){
	//free_sstfile
	if(sstfile->block_file_map){
		for(uint32_t i=0; i<sstfile->map_num; i++){
			invalidate_map_ppa(pm->bm, sstfile->block_file_map[i].ppa);
		}
		free(sstfile->block_file_map);
	}
	if(sstfile->data){
		EPRINT("data not free", true);
	}
	if(sstfile->_read_helper){
		read_helper_free(sstfile->_read_helper);
	}
	free(sstfile);
}

void sst_deep_copy(sst_file *des, sst_file *src){
//	read_helper *temp_helper=des->_read_helper;
	*des=*src;
	src->block_file_map=NULL;
	//read_helper_copy(temp_helper, src->_read_helper)
	des->_read_helper=src->_read_helper;
	//read_helper_move(des->_read_helper, src->_read_helper);
	src->_read_helper=NULL;
}
