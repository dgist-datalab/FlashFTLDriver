#include "./run.h"

extern lower_info *g_li;
void run_print(run *r, bool content){
	printf("%u:%u:%u:%.3f (recency:link:unlink)\t%u:%u:%u (sid:ste->now:ste->max)\n", 
			r->info->recency, r->info->linked_lba_num, r->info->unlinked_lba_num, (double)r->info->unlinked_lba_num/r->info->linked_lba_num,
			r->st_body->sid, r->st_body->now_STE_num, r->st_body->max_STE_num);
	if(content){
		st_array *sa=r->st_body;
		char data[PAGESIZE];
		for(uint32_t i=0; i<sa->now_STE_num; i++){
			printf("summary_page ppa: %u\n", sa->sp_meta[i].piece_ppa);

			g_li->read_sync(TEST_IO, sa->sp_meta[i].piece_ppa, data);
			summary_pair *pair=(summary_pair*)data;
			for(uint32_t idx=0; pair[idx].lba!=UINT32_MAX && idx<MAX_CUR_POINTER; ++idx ){
				printf("%u : %u\n",pair[idx].lba, 
						run_translate_intra_offset(r, i,pair[idx].piece_ppa));
			}
		}
	}
}

uint64_t run_memory_usage(run *target_run, uint32_t target_bit){
	uint64_t memory_usage_run=0;
	if(!target_run) return 0;
	if(target_run->type==RUN_LOG){
		if(target_run->run_log_mf->memory_usage_bit){
			memory_usage_run=target_run->run_log_mf->memory_usage_bit;
			return memory_usage_run;
		}
		memory_usage_run=target_run->run_log_mf->get_memory_usage(target_run->run_log_mf, target_bit);
		target_run->run_log_mf->memory_usage_bit=memory_usage_run;
	}
	else{
		for (uint32_t k = 0; k < target_run->st_body->now_STE_num; k++)
		{
			map_function *mf = target_run->st_body->pba_array[k].mf;
			if (mf)
			{
				if(mf->memory_usage_bit){
					memory_usage_run+=mf->memory_usage_bit;
				}
				else{
					uint64_t memory_usage_bit=mf->get_memory_usage(mf, target_bit);
					memory_usage_run += memory_usage_bit;
					mf->memory_usage_bit=memory_usage_bit;
				}
			}
		}
		if (target_run->type == RUN_PINNING)
		{
			memory_usage_run += target_run->now_entry_num * target_bit;
		}
	}
	return memory_usage_run;
}