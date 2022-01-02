#include "./level.h"
#include "./lsmtree.h"
#include "./shortcut.h"

level *level_init(uint32_t level_idx, uint32_t max_run_num, uint32_t map_type){
	level *res=(level*)calloc(1, sizeof(level));
	res->level_idx=level_idx;
	res->max_run_num=max_run_num;
	res->run_array=(run**)calloc(max_run_num, sizeof(run*));
	res->recency_pointer=new std::list<uint32_t>();
	res->map_type=map_type;
	return res;
}

void level_get_compaction_target(level *lev, uint32_t run_num, run*** target){
	run **res=*target;
	if(run_num==lev->now_run_num){
		std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
		for(uint32_t i=0; i<run_num; i++){
			uint32_t idx=*iter;
			res[i]=lev->run_array[idx];
			lev->run_array[idx]=NULL;
			lev->recency_pointer->erase(iter++);
		}
	}
	else if(run_num==2){
		uint32_t first_idx=UINT32_MAX;
		uint32_t second_idx=UINT32_MAX;
		float target_ratio=-1.0f;
		for(uint32_t i=0; i<lev->now_run_num; i++){
			run *temp_run=lev->run_array[i];
			sc_info *temp_scinfo=temp_run->info;
			float invalid_ratio=(float)(temp_scinfo->unlinked_lba_num)/temp_run->now_entry_num;
			if(target_ratio<invalid_ratio){
				second_idx=first_idx;
				first_idx=i;
				target_ratio=invalid_ratio;
			}
		}

		if(target_ratio==0){
			uint32_t idx=0;
			std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
			for(uint32_t i=0; i<run_num; i++){
				if(i==0){
					first_idx=*iter;
					res[idx++]=lev->run_array[first_idx];
				}
				else{
					second_idx=*iter;
					res[idx++]=lev->run_array[second_idx];
				}
			}
			return;
		}

		/*sort run by recency*/
		uint32_t idx=0;
		std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
		for(; iter!=lev->recency_pointer->end(); ){
			if(first_idx==*iter){
				res[idx++]=lev->run_array[first_idx];
				lev->run_array[first_idx]=NULL;
				lev->recency_pointer->erase(iter++);
			}
			else if(second_idx==*iter){
				res[idx++]=lev->run_array[second_idx];
				lev->run_array[second_idx]=NULL;
				lev->recency_pointer->erase(iter++);
			}
			else{
				iter++;
			}

			if(idx==run_num){
				break;
			}
		}
	}
	else{
		EPRINT("how do i implement?", true);
	}
}

void level_free(level* lev){
	delete lev->recency_pointer;
	free(lev->run_array);
	free(lev);
}
