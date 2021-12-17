#include "./compaction.h"
#include "../../include/debug_utils.h"
#include <stdlib.h>
#include <map>

extern run **run_array;
extern uint32_t run_num;

#define RUN_INVALID_DATA_NUM(r) ((r)->info->unlinked_lba_num + (r)->invalidate_piece_num)

run* compaction_test(sc_master *sc, uint32_t merge_num, uint32_t map_type, 
		float fpr, L2P_bm *bm){
	if(shortcut_is_full(sc)){
		EPRINT("sc full error!", true);
	}

	// find run
	run **merged_set=(run**)malloc(sizeof(run*) * merge_num);
	uint32_t merge_target_list=0;
	for(uint32_t i=0; i<merge_num; i++){
		uint32_t max=0;
		run *target=NULL;
		uint32_t sc_info_idx;
		for(uint32_t j=0; j< sc->max_shortcut_num; j++){
			if(merge_target_list & (1<<j)){
				continue;
			}
			if(sc->info_set[j].r && max<RUN_INVALID_DATA_NUM(sc->info_set[j].r)){
				max=RUN_INVALID_DATA_NUM(sc->info_set[j].r);
				target=sc->info_set[j].r;
				sc_info_idx=j;
			}
		}
		merged_set[i]=target;
		merge_target_list|=(1<<sc_info_idx);
	}

	// sort merged_set
	for(uint32_t i=0; i<merge_num-1; i++){
		for(uint32_t j=i+1; j<merge_num; j++){
			if(merged_set[i]->info->recency < merged_set[j]->info->recency){
				run *temp=merged_set[i];
				merged_set[i]=merged_set[j];
				merged_set[j]=temp;
			}
		}
	}
	
	printf("\n");
	for(uint32_t i=0; i<merge_num; i++){
		run_print(merged_set[i], false);
	}

	run *res=run_merge(merge_num, merged_set, GUARD_BF, fpr, bm, RUN_PINNING);
	
	for(uint32_t i=0; i<merge_num; i++){
		for(uint32_t j=0; j<run_num; j++){
			if(run_array[j]==merged_set[i]){
				run_array[j]=NULL;
			}
		}
		run_free(merged_set[i],sc);
	}
	free(merged_set);
	return res;
}
