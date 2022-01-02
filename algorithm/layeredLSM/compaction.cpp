#include "./compaction.h"
#include "../../include/debug_utils.h"
#include <stdlib.h>
#include <map>

extern run **run_array;
extern uint32_t run_num;

#define RUN_INVALID_DATA_NUM(r) ((r)->info->unlinked_lba_num + (r)->invalidate_piece_num)

void __compaction_another_level(lsmtree *lsm, uint32_t start_idx){
	uint32_t disk_idx = start_idx;
	while (level_is_full(lsm->disk[disk_idx]) && disk_idx != lsm->param.total_level_num){
		bool last_level_compaction = (disk_idx == lsm->param.total_level_num - 1);
		uint32_t des_disk_idx = last_level_compaction ? disk_idx : disk_idx + 1;

		level *src_level = lsm->disk[disk_idx];
		level *des_level = lsm->disk[des_disk_idx];

		uint32_t target_src_num = last_level_compaction ? 2 : src_level->now_run_num;
		run **merge_src = (run **)malloc(sizeof(run *) * target_src_num);
		if(target_src_num==2){
			GDB_MAKE_BREAKPOINT;
		}
		level_get_compaction_target(src_level, target_src_num, &merge_src);

		uint32_t total_target_entry = 0;
		for (uint32_t i = 0; i < target_src_num; i++)
		{
			total_target_entry += merge_src[i]->now_entry_num;
		}

		run *des = __lsm_populate_new_run(lsm, lsm->disk[disk_idx + 1]->map_type, RUN_NORMAL, total_target_entry);

		run_merge(target_src_num, merge_src, des, lsm);

		level *new_level = level_init(src_level->level_idx, src_level->max_run_num, src_level->map_type);
		if (last_level_compaction){
			std::list<uint32_t>::reverse_iterator iter = src_level->recency_pointer->rbegin();
			for (; iter != src_level->recency_pointer->rend(); iter++){
				run *r = src_level->run_array[*iter];
				level_insert_run(new_level, r);
			}
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
		disk_idx++;
	}
}

void compaction_flush(lsmtree *lsm, run *r)
{
	bool pinning_enable = __lsm_pinning_enable(lsm, r->now_entry_num);
	run *new_run = __lsm_populate_new_run(lsm, lsm->disk[0]->map_type, pinning_enable ? RUN_PINNING : RUN_NORMAL, r->now_entry_num);
	run_recontstruct(lsm, r, new_run);
	__lsm_free_run(lsm, r);
	level_insert_run(lsm->disk[0], new_run);
	
	__compaction_another_level(lsm, 0);
}