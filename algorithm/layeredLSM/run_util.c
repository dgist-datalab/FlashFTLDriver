#include "./run.h"

extern lower_info *g_li;
void run_print(run *r){
	st_array *sa=r->st_body;
	char data[PAGESIZE];
	for(uint32_t i=0; i<sa->now_STE_num; i++){
		printf("summary_page ppa: %u\n", sa->sp_meta[i].ppa);
		
		g_li->read_sync(TEST_IO, sa->sp_meta[i].ppa, data);
		summary_pair *pair=(summary_pair*)data;
		for(uint32_t idx=0; pair[idx].lba!=UINT32_MAX && idx<MAX_CUR_POINTER; ++idx ){
			printf("%u : %u\n",pair[idx].lba, 
					run_translate_intra_offset(r, pair[idx].intra_offset));
		}
	}
}
