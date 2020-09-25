#include "slap_page.h"
#include "tag_q.h"
#include "../container.h"
#include <stdlib.h>
#include <stdio.h>

static spm my_spm;

void spm_init(uint32_t numof_physical_page){
	my_spm.tag_m=tag_manager_init(numof_physical_page);
	uint64_t target_size=(uint64_t)numof_physical_page*PAGESIZE;
	my_spm.storage=(char**)malloc(sizeof(char*)*numof_physical_page);
	for(uint32_t i=0; i<numof_physical_page; i++){
		my_spm.storage[i]=(char*)malloc(PAGESIZE);
		memset(my_spm.storage[i], 0, PAGESIZE);
	}
	my_spm.allocated_size=target_size;
	my_spm.nop=numof_physical_page;
}

void spm_free(){
	tag_manager_free_manager(my_spm.tag_m);
	for(uint32_t i=0; i<my_spm.nop; i++){
		free(my_spm.storage[i]);
	}
}

int spm_memory_alloc(int type, char **buf){
	int tag_num=tag_manager_get_tag(my_spm.tag_m);
	(*buf)=my_spm.storage[tag_num];
	if((uint64_t)tag_num*PAGESIZE > my_spm.allocated_size){
		abort();
	}
	return tag_num;

}

void spm_memory_free(int type, int tag){
	tag_manager_free_tag(my_spm.tag_m, tag);
}
