#include "./run.h"

void run_print(run *r){
	st_array *sa=r->st_body;
	for(uint32_t i=0; i<sa->now_STE_num; i++){
		printf("summary_page ppa: %u\n", sa->sp_meta[i].ppa);
	}
}
