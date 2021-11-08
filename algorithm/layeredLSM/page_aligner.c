#include "page_aligner.h"
pp_buffer *pp_init(){
	pp_buffer *res=(pp_buffer*)calloc(1, sizeof(pp_buffer));
	return res;
}

void pp_free(pp_buffer *pp){
	free(pp);
}

bool pp_insert_value(pp_buffer *pp, uint32_t lba, char *value){
	uint32_t now_idx=pp->buffered_num;
	pp->LBA[now_idx]=lba;
	memcpy(&pp->value[now_idx*LPAGESIZE], value, LPAGESIZE);

	pp->buffered_num++;
	if(pp->buffered_num==L2PGAP){
		return true;
	}
	else return false;
}

value_set *pp_get_write_target(pp_buffer *pp, bool force){
	if(force){
		return inf_get_valueset(pp->value, FS_MALLOC_W, PAGESIZE);
	}
	if(pp->buffered_num!=L2PGAP){
		EPRINT("it may cause overhead", false);
	}
	return inf_get_valueset(pp->value, FS_MALLOC_W, PAGESIZE);
}

char *pp_find_value(pp_buffer *pp, uint32_t lba){
	for(uint32_t i=0; i<pp->buffered_num; i++){
		if(pp->LBA[i]==lba){
			return &pp->value[i*LPAGESIZE];
		}
	}
	return NULL;
}

void pp_reinit_buffer(pp_buffer *pp){
	memset(pp->LBA,-1, L2PGAP*sizeof(uint32_t));
	pp->buffered_num=0;
}
