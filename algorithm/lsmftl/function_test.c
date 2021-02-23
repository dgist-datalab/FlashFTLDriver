#include "function_test.h"
#include "key_value_pair.h"
#include "io.h"
#include "lsmtree.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

void LSM_traversal(lsmtree *lsm){
	lsmtree_parameter *param=&lsm->param;
	for(uint32_t i=0; i<param->LEVELN-1; i++){
		level *disk=lsm->disk[i];
		run *rptr; uint32_t r_idx;
		printf("level #%d:\n", i);
		for_each_run(disk, rptr, r_idx){
			sst_file *sptr; uint32_t s_idx;
			uint32_t prev=UINT32_MAX;
			printf("\trun #%d:\n", r_idx);
			for_each_sst(rptr, sptr, s_idx){
				printf("\t\tsstfile #%d:\n", s_idx);
				char map_data[PAGESIZE], value_data[PAGESIZE];
				key_ptr_pair *kp_ptr; uint32_t kp_idx;
				io_manager_test_read(sptr->ppa, map_data, TEST_IO);
				for_each_kp(map_data, kp_ptr, kp_idx){
					printf("\t\t\tkp #%d: %u->%u\n", kp_idx, kp_ptr->lba, kp_ptr->piece_ppa);

					if(prev==UINT32_MAX){
						prev=kp_ptr->lba;
					}
					else{
						if(prev>=kp_ptr->lba){
							EPRINT("sorting fail!", true);
						}
					}	

					io_manager_test_read(PIECETOPPA(kp_ptr->piece_ppa), value_data, TEST_IO);
					if(kp_ptr->lba!=*(uint32_t*)&value_data[4096*(kp_ptr->piece_ppa%2)]){
						EPRINT("data fail", true);
					}
					
					prev=kp_ptr->lba;
				}
			}
		}	
	}
}
