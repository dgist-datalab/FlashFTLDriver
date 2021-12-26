#include "./run.h"

extern lower_info *g_li;
void run_print(run *r, bool content){
	printf("%u:%u (v:i)\t%u:%u:%u (recency:link:unlink)\t%u:%u:%u (sid:ste->now:ste->max)\n", 
			r->validate_piece_num, r->invalidate_piece_num,
			r->info->recency, r->info->linked_lba_num, r->info->unlinked_lba_num,
			r->st_body->sid, r->st_body->now_STE_num, r->st_body->max_STE_num);
	if(content){
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
}

void run_print_invalidate_piece_ppa(run *r, blockmanager *sm){
	uint32_t cnt=0;
	for(uint32_t i=0; i< r->mf->now_contents_num; i++){
		uint32_t psa=run_translate_intra_offset(r, i);
		if(sm->is_invalid_piece(sm, psa)){
			printf("[%u:%u] %u blk:%u seg:%u\n", i, ++cnt, psa, psa/L2PGAP/_PPB, psa/L2PGAP/_PPS);
		}
	}
}

void run_check(run *r, blockmanager *sm){
	int32_t cnt=0;
	for(uint32_t i=0; i< r->mf->now_contents_num; i++){
		uint32_t psa=run_translate_intra_offset(r, i);
		if(sm->is_invalid_piece(sm, psa)){
			cnt++;
		}
	}

	if(ABS((int)r->invalidate_piece_num-cnt) > 100){
		EPRINT("error!", true);
	}
}
