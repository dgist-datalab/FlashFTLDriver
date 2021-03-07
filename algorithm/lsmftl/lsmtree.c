#include "lsmtree.h"
#include "io.h"
#include <stdlib.h>
lsmtree LSM;
char all_set_page[PAGESIZE];
void *lsmtree_read_end_req(request *req);

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
		LSM.disk[i]=level_init(now_level_size, i==LSM.param.LEVELN-1?sf:1,
				i==LSM.param.LEVELN-1, i);
		now_level_size*=sf;
		printf("L[%d] - size:%u\n",i, LSM.disk[i]->max_sst_num);
		real_mapping_num+=LSM.disk[i]->max_sst_num;

		fdriver_mutex_init(&LSM.level_lock[i]);
	}
	printf("target mapping num:%u - real_mapping_num:%u\n", LSM.param.mapping_num, real_mapping_num);


	memset(all_set_page, 1, PAGESIZE);

	LSM.monitor.compaction_cnt=(uint32_t*)calloc(LSM.param.LEVELN, sizeof(uint32_t));
	return 1;
}

void lsmtree_destroy(lower_info *li, algorithm *){
	for(uint32_t i=0; i<2; i++){
		write_buffer_free(LSM.wb_array[i]);
	}
	for(uint32_t  i=0; i<LSM.param.LEVELN; i++){
		level_free(LSM.disk[i]);
		fdriver_destroy(&LSM.level_lock[i]);
	}	
	page_manager_free(LSM.pm);
	compaction_free(LSM.cm);
}

static inline algo_req *get_read_alreq(request *const req, bool type, 
		lsmtree_read_param *r_param){
	algo_req *res=(algo_req *)calloc(1,sizeof(algo_req));
	res->type=type;
	res->param=(void*)r_param;
	res->end_req=lsmtree_read_end_req;
	res->parents=req;
	return res;
}

uint32_t lsmtree_read(request *const req){
	/*find data from write_buffer*/
	for(uint32_t i=0; i<2; i++){
		char *target;
		if((target=write_buffer_get(LSM.wb_array[i], req->key))){
			printf("find in write_buffer");
			memcpy(req->value->value, target, LPAGESIZE);
			goto hit_end;
		}
	}
	lsmtree_read_param *r_param;
	if(!req->param){
		r_param=(lsmtree_read_param*)calloc(1, sizeof(lsmtree_read_param));
		req->param=(void*)r_param;
		r_param->prev_level=-1;
		r_param->prev_run=0;
	}
	else{
		r_param=(lsmtree_read_param*)req->param;
	}
	
	algo_req *al_req;
	uint32_t piece_ppa=UINT32_MAX;
	switch(r_param->check_map){
		case K2VMAP:
			piece_ppa=kp_find_piece_ppa(req->key, req->value->value);
			if(piece_ppa==UINT32_MAX){//not found
				r_param->check_map=NOCHECK;
			}
			else{//FOUND
				al_req=get_read_alreq(req, DATAR, r_param);
				io_manager_issue_read(PIECETOPPA(piece_ppa), req->value, al_req, false);
				goto normal_end;
			}
			break;
		case DATA:
			break;
		case PLR:
			break;
		case NOCHECK:
			break;
	}
	
	level *target=NULL;
	run *rptr=NULL;
retry:
	if((target && rptr) &&
		(r_param->prev_lev==LSM.param.LEVELN-1)&& 
		(target->array[target->run_num]==rptr)){

		goto notfound;
	}


	if(r_param->prev_lev==LSM.param.LEVELN-1){
		target=LSM.disk[r_param->prev_lev];
		rptr=&target->array[++r_param->prev_run];
	}
	else{
		target=LSM.disk[++r_param->prev_lev];
		rptr=&target->array[0];
	}

	sst_file *sptr;
	if(target->istier){
		sptr=level_retrieve_sst(target, req->key);
	}
	else{
		sptr=run_retrieve_sst(rptr, req->key);
	}

	if(sptr->_read_helper){
		EPRINT("no implemented", true);
	}
	else{
		r_param->check_type=K2VMAP;
		al_req=get_read_alreq(req, MAPPINGR, r_param);
		io_manager_issue_read(sptr.file_addr.map_ppa, req->value, al_req, false);
		goto normal_end;
	}

notfound:
	printf("req->key: %u-", req->key);
	EPRINT("not found key", false);
	req->type=FS_NOTFOUND_T;

hit_end:
	req->end_req(req);

normal_end:
	return 1;
}


uint32_t lsmtree_write(request *const req){
#ifdef DEBUG
	memcpy(req->value->value, &req->key, sizeof(req->key));
#endif
	write_buffer *wb=LSM.wb_array[LSM.now_wb];
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


void lsmtree_compaction_end_req(compaction_req* req){
	free(req);
}

uint32_t lsmtree_flush(request *const req){
	printf("not implemented!!\n");
	return 1;
}

uint32_t lsmtree_remove(request *const req){
	printf("not implemented!!\n");
	return 1;
}

void *lsmtree_read_end_req(request *req){

}
