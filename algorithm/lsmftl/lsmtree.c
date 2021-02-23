#include "lsmtree.h"
#include "io.h"
#include <stdlib.h>
lsmtree LSM;
struct algorithm lsm_ftl={
	.argument_set=lsmtree_argument_set,
	.create=lsmtree_create,
	.destroy=lsmtree_destroy,
	.read=lsmtree_read,
	.write=lsmtree_write,
	.flush=lsmtree_flush,
	.remove=lsmtree_remove,
};

uint32_t lsmtree_argument_set(int argc, char *argv[]){
	return 1; 
}

uint32_t lsmtree_create(lower_info *li, blockmanager *bm, algorithm *){
	io_manager_init(li);
	LSM.pm=page_manager_init(bm);
	LSM.cm=compaction_init(1);
	LSM.wb_array=(write_buffer**)malloc(sizeof(write_buffer*) * 2);
	LSM.now_wb=0;
	for(uint32_t i=0; i<2; i++){
		LSM.wb_array[i]=write_buffer_init(1024, LSM.pm);
	}

	LSM.param.LEVELN=5;
	LSM.param.mapping_num=SHOWINGSIZE/LPAGESIZE/(PAGESIZE/sizeof(key_ptr_pair));

	LSM.disk=(level**)calloc(LSM.param.LEVELN, sizeof(level*));
	LSM.level_lock=(fdriver_lock_t*)malloc(LSM.param.LEVELN * sizeof(fdriver_lock_t));

	uint32_t sf=LSM.param.size_factor=get_size_factor(LSM.param.LEVELN, LSM.param.mapping_num);
	uint32_t now_level_size=sf;
	uint32_t real_mapping_num=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		LSM.disk[i]=level_init(now_level_size, 1, i==LSM.param.LEVELN-1, i);
		now_level_size*=sf;
		printf("L[%d] - size:%u\n",i, LSM.disk[i]->max_sst_num);
		real_mapping_num+=LSM.disk[i]->max_sst_num;

		fdriver_mutex_init(&LSM.level_lock[i]);
	}
	printf("target mapping num:%u - real_mapping_num:%u\n", LSM.param.mapping_num, real_mapping_num);

	return 1;
}

void lsmtree_destroy(lower_info *li, algorithm *){
	for(uint32_t i=0; i<2; i++){
		write_buffer_free(LSM.wb_array[i]);
	}
	page_manager_free(LSM.pm);
	compaction_free(LSM.cm);
}

uint32_t lsmtree_read(request *const req){
	/*find data from write_buffer*/
	for(uint32_t i=0; i<2; i++){
		if(write_buffer_get(LSM.wb_array[i], req->key)){
			printf("find in write_buffer");
		}
	}
	req->end_req(req);
	return 1;
}


uint32_t lsmtree_write(request *const req){
	write_buffer *wb=LSM.wb_array[LSM.now_wb];
//	if(req->key==1024 || req->key==1023){
//		printf("break!\n");
//	}
	write_buffer_insert(wb, req->key, req->value);
//	printf("write lba:%u\n", req->key);

	if(write_buffer_isfull(wb)){
		key_ptr_pair *kp_set=write_buffer_flush(wb,false);
		/*
		for(int32_t i=0; i<wb->buffered_entry_num; i++){
			printf("%u -> %u\n", kp_set[i].lba, kp_set[i].piece_ppa);	
		}*/
		write_buffer_reinit(wb);
		if(++LSM.now_wb==2){
			LSM.now_wb=0;
		}
		compaction_issue_req(LSM.cm,MAKE_L0COMP_REQ(kp_set, NULL));
	}
	req->value=NULL;
	req->end_req(req);
	return 1;
}


void lsmtree_compaction_end_req(compaction_req*){

}

uint32_t lsmtree_flush(request *const req){
	printf("not implemented!!\n");
	return 1;
}

uint32_t lsmtree_remove(request *const req){
	printf("not implemented!!\n");
	return 1;
}
