#include "lsmtree.h"
#include "io.h"
#include <stdlib.h>
lsmtree LSM;
char all_set_page[PAGESIZE];
void *lsmtree_read_end_req(algo_req *req);
extern uint32_t debug_lba;

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
	LSM.level_rwlock=(rwlock*)malloc(LSM.param.LEVELN * sizeof(rwlock));

	uint32_t sf=LSM.param.size_factor=get_size_factor(LSM.param.LEVELN, LSM.param.mapping_num);
	uint32_t now_level_size=sf;
	uint32_t real_mapping_num=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		LSM.disk[i]=level_init(now_level_size, i==LSM.param.LEVELN-1?sf:1,
				i==LSM.param.LEVELN-1, i);
		now_level_size*=sf;
		printf("L[%d] - size:%u\n",i, LSM.disk[i]->max_sst_num);
		real_mapping_num+=LSM.disk[i]->max_sst_num;
	
		rwlock_init(&LSM.level_rwlock[i]);
	}
	printf("target mapping num:%u - real_mapping_num:%u\n", LSM.param.mapping_num, real_mapping_num);


	memset(all_set_page, 1, PAGESIZE);

	LSM.monitor.compaction_cnt=(uint32_t*)calloc(LSM.param.LEVELN, sizeof(uint32_t));

	/*rhp setting*/
	LSM.param.leveling_rhp.type=HELPER_BF_PTR_GUARD;
	LSM.param.leveling_rhp.target_prob=TARGETFPR/LSM.param.LEVELN;
	LSM.param.leveling_rhp.member_num=KP_IN_PAGE;

	LSM.param.tiering_rhp.type=HELPER_BF_ONLY_GUARD;
	LSM.param.tiering_rhp.target_prob=TARGETFPR/LSM.param.LEVELN;
	LSM.param.tiering_rhp.member_num=KP_IN_PAGE;

	LSM.li=li;
	return 1;
}

void lsmtree_destroy(lower_info *li, algorithm *){
	for(uint32_t i=0; i<2; i++){
		write_buffer_free(LSM.wb_array[i]);
	}
	for(uint32_t  i=0; i<LSM.param.LEVELN; i++){
		level_free(LSM.disk[i]);
		rwlock_destroy(&LSM.level_rwlock[i]);
	}	
	page_manager_free(LSM.pm);
	compaction_free(LSM.cm);
}

static inline algo_req *get_read_alreq(request *const req, uint8_t type, 
		uint32_t physical_address, lsmtree_read_param *r_param){
	algo_req *res=(algo_req *)calloc(1,sizeof(algo_req));
	res->ppa=physical_address;
	res->type=type;
	res->param=(void*)r_param;
	res->end_req=lsmtree_read_end_req;
	res->parents=req;
	return res;
}

static void lsmtree_select_target_place(lsmtree_read_param *r_param, level **lptr, 
		run **rptr, sst_file **sptr, uint32_t lba){

	if((*lptr) && (*lptr)->idx==4){
		printf("break!\n");
	}

	rwlock *target;
retry:
	if(r_param->target_level_rw_lock){
		rwlock_read_unlock(r_param->target_level_rw_lock);
	}

	if(r_param->prev_level==LSM.param.LEVELN-1){
		target=&LSM.level_rwlock[r_param->prev_level];
		rwlock_read_lock(target);
		*lptr=LSM.disk[r_param->prev_level];
		*rptr=&(*lptr)->array[++r_param->prev_run];
	}
	else{
		target=&LSM.level_rwlock[++r_param->prev_level];
		rwlock_read_lock(target);
		*lptr=LSM.disk[r_param->prev_level];
		*rptr=&(*lptr)->array[0];
	}

	if(!(*lptr)->istier){
		*sptr=level_retrieve_sst(*lptr, lba);
	}
	else{
		if(r_param->prev_run > ((*lptr)->run_num)){
			printf("not find :%u ???\n", lba);
			EPRINT("not found eror!", true);
		}
		*sptr=run_retrieve_sst(*rptr, lba);
	}

	if(*sptr==NULL){
		goto retry;
	}

	r_param->prev_sf=*sptr;
	r_param->target_level_rw_lock=target;
	r_param->read_helper_idx=read_helper_idx_init((*sptr)->_read_helper, lba);
}

uint32_t lsmtree_read(request *const req){
	lsmtree_read_param *r_param;
	if(req->key==debug_lba){
		printf("break!\n");
	}
	if(!req->param){
		//printf("read key:%u\n", req->key);
		/*find data from write_buffer*/
		for(uint32_t i=0; i<2; i++){
			char *target;
			if((target=write_buffer_get(LSM.wb_array[i], req->key))){
			//	printf("find in write_buffer");
				memcpy(req->value->value, target, LPAGESIZE);
				req->end_req(req);
				return 1;
			}
		}

		r_param=(lsmtree_read_param*)calloc(1, sizeof(lsmtree_read_param));
		req->param=(void*)r_param;
		r_param->prev_level=-1;
		r_param->prev_run=0;
		r_param->target_level_rw_lock=NULL;
	}
	else{
		r_param=(lsmtree_read_param*)req->param;
	}

	algo_req *al_req;
	level *target=NULL;
	run *rptr=NULL;
	sst_file *sptr=NULL;
	uint32_t read_target_ppa=UINT32_MAX; //map_ppa && map_piece_ppa

	switch(r_param->check_type){
		case K2VMAP:
			read_target_ppa=kp_find_piece_ppa(req->key, req->value->value);
			if(read_target_ppa==UINT32_MAX){//not found
				r_param->check_type=NOCHECK;
			}
			else{//FOUND
				al_req=get_read_alreq(req, DATAR, read_target_ppa, r_param);
				io_manager_issue_read(PIECETOPPA(read_target_ppa), req->value, al_req, false);
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
	
retry:
	if((target && rptr) &&
		(r_param->prev_level==LSM.param.LEVELN-1)&& 
		(&target->array[target->run_num]==rptr)){
		goto notfound;
	}

	if(r_param->use_read_helper){
		if(!read_helper_last(r_param->prev_sf->_read_helper, r_param->read_helper_idx)){
			sptr=r_param->prev_sf;
			goto read_helper_check_again;
		}
	}
	
	lsmtree_select_target_place(r_param, &target, &rptr, &sptr, req->key);
		
	if(!sptr){
		goto retry;
	}
	
read_helper_check_again:

	if(sptr->_read_helper){
		/*issue data read!*/
		r_param->use_read_helper=true;
		if(read_helper_check(sptr->_read_helper, req->key, &read_target_ppa, sptr, &r_param->read_helper_idx)){
			al_req=get_read_alreq(req, DATAR, read_target_ppa, r_param);
			io_manager_issue_read(PIECETOPPA(read_target_ppa), req->value, al_req, false);
			goto normal_end;
		}else{
			lsmtree_select_target_place(r_param, &target, &rptr, &sptr, req->key);
			goto retry;
		}
	}
	else{
		/*issue map read!*/
		read_target_ppa=target->istier?
			sst_find_map_addr(sptr, req->key):sptr->file_addr.map_ppa;
		if(read_target_ppa==UINT32_MAX){
			goto notfound;
		}
		r_param->check_type=K2VMAP;
		al_req=get_read_alreq(req, MAPPINGR, read_target_ppa, r_param);
		io_manager_issue_read(read_target_ppa, req->value, al_req, false);
		goto normal_end;
	}

notfound:
	printf("req->key: %u-", req->key);
	EPRINT("not found key", false);
	req->type=FS_NOTFOUND_T;
	req->end_req(req);
	return 0;

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

void *lsmtree_read_end_req(algo_req *req){
	request *parents=req->parents;
	lsmtree_read_param *r_param=(lsmtree_read_param*)req->param;
	uint32_t piece_ppa;
	uint32_t idx;
	bool read_done=false;
	switch(req->type){
		case MAPPINGR:
			parents->type_ftl++;
			if(!inf_assign_try(parents)){
				EPRINT("why cannot retry?", true);
			}
			break;
		case DATAR:
			piece_ppa=req->ppa;
			if(page_manager_oob_lba_checker(LSM.pm, piece_ppa, parents->key, &idx)){
				rwlock_read_unlock(r_param->target_level_rw_lock);
				if(idx>=L2PGAP){
					EPRINT("can't be plz checking oob_lba_checker", true);
				}
				if(idx){
					memcpy(parents->value->value, &parents->value->value[idx*LPAGESIZE], LPAGESIZE);
				}
				read_done=true;
			}
			else{
				LSM.li->req_type_cnt[MISSDATAR]++;
				parents->type_ftl++;
				if(!inf_assign_try(parents)){
					EPRINT("why cannot retry?", true);
				}	
			}
			break;
	}
	if(read_done){
		parents->end_req(parents);
		free(r_param);
	}
	free(req);
	return NULL;
}


void lsmtree_level_summary(lsmtree *lsm){
	for(uint32_t i=0; i<lsm->param.LEVELN; i++){
		printf("ptr:%p ", lsm->disk[i]); level_print(lsm->disk[i]);
	}
}
