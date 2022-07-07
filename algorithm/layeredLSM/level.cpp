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
extern bool running_flag;
extern uint32_t gc_type;
void level_get_compaction_target(level *lev, uint32_t run_num, run*** target, bool old_first){
	run **res=*target;
	if(run_num==lev->now_run_num){
		std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
		for(uint32_t i=0; i<run_num; i++){
			uint32_t idx=*iter;
			res[i]=lev->run_array[idx];
			lev->run_array[idx]=NULL;
			lev->now_run_num--;
			lev->recency_pointer->erase(iter++);
		}
	}
	else if(run_num==2){
		uint32_t first_idx=UINT32_MAX;
		uint32_t second_idx=UINT32_MAX;

		float first_average_inv_ratio=0;
		float second_average_inv_ratio=0;
		float max_inv_ratio=0;
		uint32_t sum_cnt=0;

		if(running_flag){
			for(uint32_t i=0; i<lev->now_run_num; i++){
				run *temp_run = lev->run_array[i];
				if(temp_run==NULL) continue;
				sc_info *temp_scinfo = temp_run->info;
				float invalid_ratio = (float)(temp_scinfo->unlinked_lba_num) / temp_run->now_entry_num;			
				first_average_inv_ratio+=invalid_ratio;
				sum_cnt++;
				if(invalid_ratio> max_inv_ratio){
					max_inv_ratio=invalid_ratio;
				}
				printf("%f -> %u\n", invalid_ratio, temp_run->now_entry_num);
			}
		}

		second_average_inv_ratio=first_average_inv_ratio;
		first_average_inv_ratio/=sum_cnt;
		second_average_inv_ratio-=max_inv_ratio;
		second_average_inv_ratio/=(sum_cnt-1);
		uint32_t copy_entry_num=UINT32_MAX;
		float target_ratio=-1.0f;
		uint32_t temp_run_num=run_num;
		if (old_first == false)
		{
			for (uint32_t round = 0; round < temp_run_num; round++)
			{
				target_ratio = -1.0f;
				copy_entry_num = UINT32_MAX;
				for (uint32_t i = 0; i < lev->now_run_num; i++)
				{
					run *temp_run = lev->run_array[i];
					if (temp_run == NULL)
						continue;
					sc_info *temp_scinfo = temp_run->info;
					float invalid_ratio = (float)(temp_scinfo->unlinked_lba_num) / temp_run->now_entry_num;
					if (gc_type == 0)
					{
						if (target_ratio < invalid_ratio)
						{
							if (round == 0)
							{
								first_idx = i;
								target_ratio = invalid_ratio;
							}
							else if (round == 1)
							{
								if (first_idx == i)
								{
									continue;
								}
								else
								{
									second_idx = i;
									target_ratio = invalid_ratio;
								}
							}
						}
					}
					else
					{
						uint32_t now_copy_entry_num = temp_run->now_entry_num - temp_scinfo->unlinked_lba_num;
						if (round < run_num)
						{
							if (invalid_ratio >= first_average_inv_ratio && copy_entry_num > now_copy_entry_num)
							{
								if (round == 0)
								{
									first_idx = i;
									copy_entry_num = now_copy_entry_num;
								}
								else if (round == 1)
								{
									if (first_idx == i)
									{
										continue;
									}
									else
									{
										second_idx = i;
										copy_entry_num = now_copy_entry_num;
									}
								}
							}
						}
						else
						{
							if (invalid_ratio >= second_average_inv_ratio && copy_entry_num > now_copy_entry_num)
							{
								if (first_idx == i)
								{
									continue;
								}
								else
								{
									second_idx = i;
									copy_entry_num = now_copy_entry_num;
								}
							}
						}
					}
				}
				if (round == run_num - 1 && second_idx == UINT32_MAX)
				{
					temp_run_num++;
				}
			}
		}
		else
		{
			target_ratio = 0;
		}

		if(target_ratio==0){
			uint32_t idx=0;
			uint32_t run_idx=0;
			std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
			for(uint32_t i=0; i<run_num; i++){
				run_idx = *iter;
				res[idx++] = lev->run_array[run_idx];
				lev->run_array[run_idx] = NULL;
				lev->now_run_num--;
				lev->recency_pointer->erase(iter++);
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
				lev->now_run_num--;
				lev->recency_pointer->erase(iter++);
			}
			else if(second_idx==*iter){
				res[idx++]=lev->run_array[second_idx];
				lev->run_array[second_idx]=NULL;
				lev->now_run_num--;
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

static inline uint32_t __max_invalidata_run_idx(level *lev){
	float target_ratio=-1.0f;
	uint32_t target_idx=0;
	for (uint32_t i = 0; i < lev->now_run_num; i++){
		run *temp_run = lev->run_array[i];
		if(temp_run==NULL) continue;
		sc_info *temp_scinfo = temp_run->info;
		float invalid_ratio = (float)(temp_scinfo->unlinked_lba_num) / temp_run->now_entry_num;
		if(target_ratio < invalid_ratio){
			target_idx=i;
			target_ratio=invalid_ratio;
		}
	}
	return target_idx;
}

run * level_get_max_unlinked_run(level *lev){
	uint32_t target_idx=__max_invalidata_run_idx(lev);
	run *res=lev->run_array[target_idx];
	std::list<uint32_t>::iterator iter=lev->recency_pointer->begin();
	for(;iter!=lev->recency_pointer->end(); iter++){
		if(*iter==target_idx){
			lev->run_array[target_idx]=NULL;
			lev->recency_pointer->erase(iter);
			lev->now_run_num--;
			break;
		}
	}
	return res;
}

uint32_t level_pick_max_unlinked_num(level *lev){
	uint32_t target_idx=__max_invalidata_run_idx(lev);
	run *res=lev->run_array[target_idx];
	return res->info->unlinked_lba_num;
}
