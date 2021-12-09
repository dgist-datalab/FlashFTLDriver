#include "./block_manager_master.h"

blockmanager_master BMM;

void blockmanager_master_init(){
	uint32_t block_idx=0;
	for(uint32_t j=0; j<_NOS; j++){
		for(uint32_t i=0; i<BPS; i++){
			BMM.total_block_set[block_idx].block_idx=block_idx;
			BMM.h_block_group[i].block_set[j]=&BMM.total_block_set[block_idx];
			block_idx++;
		}
	}
}

void blockmanager_master_free(){
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


void blockmanager_master_dump(FILE *fp){
	uint64_t temp_NOS=_NOS;
	uint64_t temp_NOB=_NOB;
	uint64_t temp_BPS=BPS;
	fwrite(&temp_NOS, sizeof(temp_NOS), 1, fp); //write the total umber of segment
	fwrite(&temp_NOB, sizeof(temp_NOB), 1, fp); // write the total number of blocks
	fwrite(&temp_BPS, sizeof(temp_BPS), 1, fp); //write the number of blocks in a segment
	fwrite(BMM.total_block_set, sizeof(__block),_NOB, fp);
}

void blockmanager_master_load(FILE *fp){
	uint64_t read_NOS, read_NOB, read_BPS;

	fread(&read_NOS, sizeof(read_NOS), 1, fp);
	fread(&read_NOB, sizeof(read_NOS), 1, fp);
	fread(&read_BPS, sizeof(read_NOS), 1, fp);

	if(read_NOS!=_NOS || read_NOB!=_NOB || read_BPS!=BPS){
		EPRINT("different device setting", true);
	}

	fread(BMM.total_block_set, sizeof(__block), _NOB, fp);
	for(uint32_t i=0; i<_NOB; i++){
		BMM.total_block_set[i].private_data=NULL;
	}
}
