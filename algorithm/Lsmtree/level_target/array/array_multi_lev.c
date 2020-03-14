#include "array.h"
void array_multi_merger(skiplist *skip,run_t *r,bool last){
	uint16_t *bitmap;
	ppa_t *ppa_ptr;
	KEYT key;
	char* body;
	int idx;
	body=data_from_run(r);
	for_each_header_start(idx,key,ppa_ptr,bitmap,body)
		skiplist_insert_existIgnore(skip,key,*ppa_ptr,*ppa_ptr==UINT32_MAX?false:true);
	for_each_header_end
}

