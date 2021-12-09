#include "./block_manager_master.h"

blockmanager_master BMM;

void blockmanager_master_init(){
	uint32_t block_idx=0;
	for(uint32_t j=0; j<_NOS; j++){
		for(uint32_t i=0; i<BPS; i++){
			BMM.total_block_set[block_idx].block_idx=block_idx;
			BMM.total_block_set[block_idx].bitset=(uint8_t*)calloc(_PPB*L2PGAP/8,1);
			BMM.h_block_group[i].block_set[j]=&BMM.total_block_set[block_idx];
			block_idx++;
		}
	}
}

void blockmanager_master_free(){
	for(uint32_t i=0; i<_NOB; i++){
		free(BMM.total_block_set[i].bitset);
	}
}

struct blockmanager * blockmanager_factory(uint32_t type, lower_info *li){
	blockmanager_master_init();
	switch(type){
		case SEQ_BM:
			return sbm_create(li);
	}
	return NULL;
}

void blockmanager_free(struct blockmanager *bm){
	switch(bm->type){
		case SEQ_BM:
			sbm_free(bm);
			break;
	}
	blockmanager_master_free();
}
__block *blockmanager_master_get_block(uint32_t horizontal_block_gid, 
		__block *(*get_block)(void *)){
	return get_block(BMM.h_block_group[horizontal_block_gid].private_data);
}

bool default_check_full(__segment *s){
	return s->blocks[BPS-1]->now_assigned_pptr==_PPB;
}
