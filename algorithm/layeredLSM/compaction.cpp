#include "./compaction.h"
#include "../../include/debug_utils.h"
#include "../../bench/measurement.h"
#include "./gc.h"
#include "./sorted_table.h"
#include <stdlib.h>
#include <map>

extern run **run_array;
extern uint32_t run_num;
extern bool debug_flag;

#define RUN_INVALID_DATA_NUM(r) ((r)->info->unlinked_lba_num + (r)->invalidate_piece_num)

#define GREEDY_GC
#define BF_WISCKEY

void __compaction_another_level(lsmtree *lsm, uint32_t start_idx, bool force){
	uint32_t disk_idx = start_idx;
	uint64_t original_shortcut_memory=lsm->shortcut->now_sc_memory_usage;
	while ((force || level_is_full(lsm->disk[disk_idx])) && disk_idx != lsm->param.total_level_num){
		if(force && lsm->disk[disk_idx]->now_run_num==0){
			disk_idx++;
			continue;
		}
		bool last_level_compaction = (disk_idx == lsm->param.total_level_num - 1);
		uint32_t des_disk_idx = last_level_compaction ? disk_idx : disk_idx + 1;

		level *src_level = lsm->disk[disk_idx];
		level *des_level = lsm->disk[des_disk_idx];

		uint32_t target_src_num = last_level_compaction ? 2 : src_level->now_run_num;
		//uint32_t target_src_num = 2;
		run **merge_src = (run **)malloc(sizeof(run *) * target_src_num);

		if(last_level_compaction){
			uint32_t first_run_idx=get_old_run_idx(src_level, 0);
			uint32_t second_run_idx=get_old_run_idx(src_level, 1);
			uint32_t total_run_entry=src_level->run_array[first_run_idx]->now_entry_num+src_level->run_array[second_run_idx]->now_entry_num;
			uint32_t invalid_run_entry=src_level->run_array[first_run_idx]->info->unlinked_lba_num+src_level->run_array[second_run_idx]->info->unlinked_lba_num;
			lsm->monitor.compaction_input_entry_num[disk_idx+2]+=total_run_entry;

			lsm->monitor.compaction_output_entry_num[disk_idx+2]+=total_run_entry-invalid_run_entry;
		}

#ifdef GREEDY_GC
		level_get_compaction_target(src_level, target_src_num, &merge_src, !last_level_compaction);
#else
		level_get_compaction_target(src_level, target_src_num, &merge_src, true);
#endif
		uint32_t total_target_entry = 0;
		for (uint32_t i = 0; i < target_src_num; i++)
		{
			total_target_entry += merge_src[i]->now_entry_num;
		}
		run *des = __lsm_populate_new_run(lsm, lsm->disk[last_level_compaction? disk_idx : disk_idx + 1]->map_type, RUN_NORMAL, total_target_entry, last_level_compaction?disk_idx+1:disk_idx+2);


	//	run_merge(target_src_num, merge_src, des, false, lsm);
		run_merge_thread(target_src_num, merge_src, des, false, lsm);

		lsm->monitor.compaction_cnt[disk_idx+1]++;
		lsm->monitor.compaction_input_entry_num[disk_idx+1] += total_target_entry;
		lsm->monitor.compaction_output_entry_num[disk_idx+1] += des->now_entry_num;

		level *new_level = level_init(src_level->level_idx, src_level->max_run_num, src_level->map_type);

		if(target_src_num!=src_level->now_run_num){
			std::list<uint32_t>::reverse_iterator iter = src_level->recency_pointer->rbegin();
			for (; iter != src_level->recency_pointer->rend(); iter++){
				run *r = src_level->run_array[*iter];
				level_insert_run(new_level, r);
			}
		}

		if (last_level_compaction){
			des_level = new_level;
		}
		else{
			lsm->disk[disk_idx] = new_level;
		}

		level_insert_run(des_level, des);
	
		for (uint32_t i = 0; i < target_src_num; i++){
			__lsm_free_run(lsm, merge_src[i]);
		}
		level_free(src_level);
		lsm->disk[des_disk_idx] = des_level;
		free(merge_src);
		if(last_level_compaction) break;
		disk_idx++;
	}
	uint64_t now_shortcut_memory=lsm->shortcut->now_sc_memory_usage;
	if(original_shortcut_memory > now_shortcut_memory){
		printf("sc memory decrease! %lf\n", (double)now_shortcut_memory/original_shortcut_memory);
	}
	else if(original_shortcut_memory< now_shortcut_memory){
		printf("sc memory increase!\n");
	}
}

void compaction_flush(lsmtree *lsm, run *r)
{
#ifdef BF_WISCKEY
	bool pinning_enable = __lsm_pinning_enable(lsm, r->now_entry_num);
#else
	bool pinning_enable = false;
#endif

	run *new_run = __lsm_populate_new_run(lsm, lsm->disk[0]->map_type, pinning_enable ? RUN_PINNING : RUN_NORMAL, r->now_entry_num, 1);

	run_recontstruct(lsm, r, new_run, false);

	lsm->monitor.compaction_cnt[0]++;
	lsm->monitor.compaction_input_entry_num[0]+=r->now_entry_num;
	lsm->monitor.compaction_output_entry_num[0]+=new_run->now_entry_num;
	level_insert_run(lsm->disk[0], new_run);

	__compaction_another_level(lsm, 0, false);

	while(gc_check_enough_space(lsm->bm, (lsm->param.memtable_entry_num)/MAX_SECTOR_IN_BLOCK)==false){
		lsm->monitor.force_compaction_cnt++;
		__compaction_another_level(lsm, 0, true);
	}
	__lsm_free_run(lsm, r);
	uint64_t new_shortcut_memory=lsm->shortcut->now_sc_memory_usage;
}

void compaction_clean_last_level(lsmtree *lsm){
	uint32_t last_level_idx=lsm->param.total_level_num-1;
	run *max_unlinked_run = level_get_max_unlinked_run(lsm->disk[last_level_idx]);
	run *temp_new_run = __lsm_populate_new_run(lsm, lsm->disk[last_level_idx]->map_type, RUN_NORMAL, max_unlinked_run->now_entry_num, last_level_idx);
	run_recontstruct(lsm, max_unlinked_run, temp_new_run, true);
	__lsm_free_run(lsm, max_unlinked_run);
	level_insert_run(lsm->disk[last_level_idx], temp_new_run);
}

extern lsmtree* LSM;
void compaction_thread_run(void *arg, int idx){
	run *r=(run*)arg;
	compaction_flush(LSM, r);
}
