#include "function_test.h"
#include "key_value_pair.h"
#include "io.h"
#include "lsmtree.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static inline bool kp_data_check(char *data, uint32_t lev_idx, uint32_t run_idx, 
		uint32_t sst_idx, uint32_t find_lba, bool should_print){

	key_ptr_pair *kp_ptr; uint32_t kp_idx;
	uint32_t prev=UINT32_MAX;
	char value_data[PAGESIZE];
	for_each_kp(data, kp_ptr, kp_idx){
		if(should_print){
			printf("\t\t\tkp #%d: %u->%u\n", kp_idx, kp_ptr->lba, kp_ptr->piece_ppa);
		}


		if(prev==UINT32_MAX){
			prev=kp_ptr->lba;
		}
		else{
			if(prev>=kp_ptr->lba){
				EPRINT("sorting fail!", true);
			}
		}	

		if(kp_ptr->lba==UINT32_MAX) break;
		if(kp_ptr->lba==find_lba){
			printf("find_target:%u ppa:%u lev_idx:%u, run_idx:%u!\n", 
					find_lba, kp_ptr->piece_ppa, lev_idx, run_idx);
			return true;
		}

		io_manager_test_read(PIECETOPPA(kp_ptr->piece_ppa), value_data, TEST_IO);
		if(kp_ptr->lba!=*(uint32_t*)&value_data[L2PGAP*(kp_ptr->piece_ppa%L2PGAP)]){
			EPRINT("data fail", true);
		}

	}
	return false;
}

void LSM_traversal(lsmtree *lsm){
	lsmtree_parameter *param=&lsm->param;
	for(uint32_t i=0; i<param->LEVELN-1; i++){
		level *disk=lsm->disk[i];
		run *rptr; uint32_t r_idx;
		printf("level #%d:\n", i);
		for_each_run(disk, rptr, r_idx){
			sst_file *sptr; uint32_t s_idx;
			printf("\trun #%d:\n", r_idx);
			for_each_sst(rptr, sptr, s_idx){
				printf("\t\tsstfile #%d:\n", s_idx);
				char map_data[PAGESIZE];
				if(sptr->type==PAGE_FILE){
					io_manager_test_read(sptr->file_addr.map_ppa, map_data, TEST_IO);
					kp_data_check(map_data, i, r_idx, s_idx, UINT32_MAX, false);
				}
				else{
					map_range *mr;
					for(uint32_t mr_idx=0; mr_idx<sptr->map_num; mr_idx++){
						mr=&sptr->block_file_map[mr_idx];
						io_manager_test_read(mr->ppa, map_data, TEST_IO);
						kp_data_check(map_data, i, r_idx, s_idx, UINT32_MAX, false);
					}
				}
			}
		}	
	}
}

bool LSM_find_lba(lsmtree *lsm, uint32_t lba){
	lsmtree_parameter *param=&lsm->param;
	for(uint32_t i=0; i<=param->LEVELN-1; i++){
		level *disk=lsm->disk[i];
		run *rptr; uint32_t r_idx;
	//	printf("level #%d:\n", i);
		for_each_run(disk, rptr, r_idx){
			sst_file *sptr; uint32_t s_idx;
	//		printf("\trun #%d:\n", r_idx);
			for_each_sst(rptr, sptr, s_idx){
	//			printf("\t\tsstfile #%d:\n", s_idx);
				char map_data[PAGESIZE];
				if(sptr->type==PAGE_FILE){
					io_manager_test_read(sptr->file_addr.map_ppa, map_data, TEST_IO);
					if(kp_data_check(map_data, i, r_idx, s_idx, lba, false)){
						printf("level:%u run:%u s_idx:%u\n", i, r_idx, s_idx);
						return true;
					}
				}
				else{
					map_range *mr;
					for(uint32_t mr_idx=0; mr_idx<sptr->map_num; mr_idx++){
						mr=&sptr->block_file_map[mr_idx];
						io_manager_test_read(mr->ppa, map_data, TEST_IO);
						if(kp_data_check(map_data, i, r_idx, s_idx, lba, false)){
							printf("level:%u run:%u s_idx:%u\n", i, r_idx, s_idx);
							return true;
						}
					}
				}
			}
		}	
	}
	return false;
}

bool LSM_level_find_lba(level *lev, uint32_t lba){
	run *rptr; uint32_t r_idx;
	//	printf("level #%d:\n", i);
	for_each_run(lev, rptr, r_idx){
		sst_file *sptr; uint32_t s_idx;
		//		printf("\trun #%d:\n", r_idx);
		for_each_sst(rptr, sptr, s_idx){
			//			printf("\t\tsstfile #%d:\n", s_idx);
			char map_data[PAGESIZE];
			if(sptr->type==PAGE_FILE){
				io_manager_test_read(sptr->file_addr.map_ppa, map_data, TEST_IO);
				if(kp_data_check(map_data, lev->idx, r_idx, s_idx, lba, false))
					return true;
			}
			else{
				map_range *mr;
				for(uint32_t mr_idx=0; mr_idx<sptr->map_num; mr_idx++){
					mr=&sptr->block_file_map[mr_idx];
					io_manager_test_read(mr->ppa, map_data, TEST_IO);
					if(kp_data_check(map_data, lev->idx, r_idx, s_idx, lba, false))
						return true;
				}
			}
		}
	}
	return false;
}
