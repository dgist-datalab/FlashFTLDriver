#include "page_manager.h"
#include "lsmtree.h"
#include "io.h"
extern lsmtree LSM;
void *gc_map_check_end_req(algo_req *req){
	gc_mapping_check_node *gmc;
	switch(req->type){
		case GCMR_DGC:
			gmc=(gc_mapping_check_node*)req->param;
			fdriver_unlock(&gmc->done_lock);
			break;
		case GCMW_DGC:
			inf_free_valueset((value_set*)req->param, FS_MALLOC_W);
			break;
	}
	free(req);
	return NULL;
}

void gc_issue_mapcheck_read(gc_mapping_check_node *gmc, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)gmc;
	req->end_req=gc_map_check_end_req;
	req->type=GCMR_DGC;
	li->read(gmc->map_ppa, PAGESIZE, gmc->mapping_data, ASYNC, req);
}
#if 0
//#ifdef PINKGC
void gc_helper_for_pink(std::queue<gc_mapping_check_node*>* gc_mapping_queue){
	page_manager *pm=LSM.pm;
	write_buffer *gc_wb=write_buffer_init(_PPS*L2PGAP, pm, GC_WB);
	sst_file *map_ptr;
	uint32_t found_piece_ppa=UINT32_MAX;
	bool kp_set_check_start=false;
	uint32_t kp_set_iter=0;
	write_buffer *flushed_kp_set_update=NULL;

	gc_mapping_check_node *gmc;
	while(!gc_mapping_queue->empty()){
		gmc=gc_mapping_queue->front();
		std::deque<key_ptr_pair*>::iterator moved_kp_it;
		key_ptr_pair *moved_kp_now;
		bool moved_kp_found=false;
retry:
		switch(gmc->type){
			case MAP_CHECK_FLUSHED_KP:
				found_piece_ppa=UINT32_MAX;
				for(kp_set_iter=0;kp_set_iter<COMPACTION_REQ_MAX_NUM; kp_set_iter++){
					if(!LSM.flushed_kp_set[kp_set_iter]) continue;
					found_piece_ppa=kp_find_piece_ppa(gmc->lba, (char*)LSM.flushed_kp_set[kp_set_iter]);
					if(found_piece_ppa!=UINT32_MAX){break;}
				}
				if(found_piece_ppa==gmc->piece_ppa){
					if(!kp_set_check_start){
						kp_set_check_start=true;
						rwlock_write_lock(&LSM.flushed_kp_set_lock);
						flushed_kp_set_update=write_buffer_init(_PPS*L2PGAP, pm, GC_WB);
					}
					invalidate_piece_ppa(pm->bm, gmc->piece_ppa, true);
					write_buffer_insert_for_gc(flushed_kp_set_update, gmc->lba, gmc->data_ptr);
				}
				else{
					//check moved kp
					moved_kp_it=LSM.moved_kp_set->begin();
					for(;moved_kp_it!=LSM.moved_kp_set->end(); moved_kp_it++){
						moved_kp_now=*moved_kp_it;
						found_piece_ppa=kp_find_piece_ppa(gmc->lba, (char*)moved_kp_now);
						if(found_piece_ppa==gmc->piece_ppa){
							if(!kp_set_check_start){
								kp_set_check_start=true;
								rwlock_write_lock(&LSM.flushed_kp_set_lock);
								flushed_kp_set_update=write_buffer_init(_PPS*L2PGAP, pm, GC_WB);
							}
							invalidate_piece_ppa(pm->bm, gmc->piece_ppa, true);
							write_buffer_insert_for_gc(flushed_kp_set_update, gmc->lba, gmc->data_ptr);
							moved_kp_found=true;
							break;
						}
					}
					if(!moved_kp_found){
						gmc->type=MAP_READ_ISSUE;
						goto retry;
					}
				}
				break;
				//asdfasdf
			case MAP_READ_ISSUE:
				if((int)gmc->level<0){
					printf("gmc->lba:%u piece_ppa:%u\n", gmc->lba, gmc->piece_ppa);
					EPRINT("mapping not found", true);
				}
				map_ptr=level_retrieve_sst_with_check(LSM.disk[gmc->level], gmc->lba);
				if(!map_ptr){
					gmc->level--;
					goto retry;
				}
				else{
					gmc->type=MAP_READ_DONE;
					gmc->map_ppa=map_ptr->file_addr.map_ppa;
					gc_issue_mapcheck_read(gmc, bm->li);
					gc_mapping_queue->pop();
					gc_mapping_queue->push(gmc);
					continue;
				}
			case MAP_READ_DONE:
				fdriver_lock(&gmc->done_lock);
				found_piece_ppa=kp_find_piece_ppa(gmc->lba, gmc->mapping_data->value);
				if(gmc->piece_ppa==found_piece_ppa){
					invalidate_piece_ppa(pm->bm, gmc->piece_ppa, true);
					slm_remove_node(gmc->level, SEGNUM(gmc->piece_ppa));

					target_version=version_level_idx_to_version(LSM.last_run_version, gmc->level, LSM.param.LEVELN);
					recent_version=version_map_lba(LSM.last_run_version, gmc->lba);
					if(version_compare(LSM.last_run_version, recent_version, target_version)<=0){
						//					version_coupling_lba_ridx(LSM.last_run_version, gmc->lba, TOTALRUNIDX);
						write_buffer_insert_for_gc(gc_wb, gmc->lba, gmc->data_ptr);
					}
					else{
						goto out;
					}
				}
				else{
					gmc->type=MAP_READ_ISSUE;
					gmc->level--;
					goto retry;
				}
		}
out:
		fdriver_destroy(&gmc->done_lock);
		inf_free_valueset(gmc->mapping_data, FS_MALLOC_R);
		free(gmc);
		gc_mapping_queue->pop();
	}

	key_ptr_pair *kp_set;
	if(flushed_kp_set_update){
		while((kp_set=write_buffer_flush_for_gc(flushed_kp_set_update, false, victim->seg_idx, NULL, 
						UINT32_MAX, NULL))){
			for(uint32_t j=0; j<COMPACTION_REQ_MAX_NUM; j++){
				bool check=false;
				key_ptr_pair *target_kp_set=LSM.flushed_kp_set[j];
				for(uint32_t i=0; i<KP_IN_PAGE && kp_set[i].lba!=UINT32_MAX; i++){
					uint32_t idx=UINT32_MAX;
					if(target_kp_set){
						kp_find_idx(kp_set[i].lba, (char*)target_kp_set);
					}
					if(idx==UINT32_MAX){
						std::deque<key_ptr_pair*>::iterator moved_kp_it=LSM.moved_kp_set->begin();
						for(;moved_kp_it!=LSM.moved_kp_set->end(); moved_kp_it++){
							key_ptr_pair *temp_pair=*moved_kp_it;
							idx=kp_find_idx(kp_set[i].lba, (char*)temp_pair);
							if(idx!=UINT32_MAX){
								temp_pair[idx].piece_ppa=kp_set[i].piece_ppa;
								check=true;
								break;
							}
						}
						//check false
					}
					else{
						check=true;
						target_kp_set[idx].piece_ppa=kp_set[i].piece_ppa;
					}
					if(!check){
						printf("target pair:%u,%u\n", kp_set[i].lba, kp_set[i].piece_ppa);
						EPRINT("the pair should be in flushed_kp_set", true);
					}
				}
			}
			free(kp_set);
		}
		write_buffer_free(flushed_kp_set_update);
		rwlock_write_unlock(&LSM.flushed_kp_set_lock);
	}
	uint32_t k=0;
	while((kp_set=write_buffer_flush_for_gc(gc_wb, false, victim->seg_idx, NULL,UINT32_MAX, NULL))){
		k++;
		for(uint32_t i=0; i<KP_IN_PAGE && kp_set[i].lba!=UINT32_MAX; i++){
			if(kp_set[i].lba==807091 || kp_set[i].lba==464699){
				printf("%u target %u reinsert at %u\n", k, kp_set[i].lba, kp_set[i].piece_ppa);
			}
		}
		fdriver_lock(&LSM.moved_kp_lock);
		LSM.moved_kp_set->push_back(kp_set);
		fdriver_unlock(&LSM.moved_kp_lock);
	}
	write_buffer_free(gc_wb);
}
#endif

static void sst_file_update_mapping(sst_file *sptr, value_set *vs, uint32_t level, uint32_t max_piece_ppa, page_manager *pm){
	invalidate_map_ppa(pm->bm, sptr->file_addr.map_ppa, true);
	sptr->file_addr.map_ppa=page_manager_get_new_ppa(LSM.pm, true, MAPSEG);
	validate_map_ppa(pm->bm, sptr->file_addr.map_ppa, sptr->start_lba, sptr->end_lba , true);

	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=GCMW_DGC;
	write_req->param=(void*)vs;
	write_req->end_req=gc_map_check_end_req;

	io_manager_issue_internal_write(sptr->file_addr.map_ppa, vs, write_req, false);
	slm_coupling_level_seg(level, SEGNUM(max_piece_ppa), SEGPIECEOFFSET(max_piece_ppa), false);
}

void gc_helper_for_normal(std::map<uint32_t, gc_mapping_check_node*> *gkv, 
		write_buffer *wb,
		uint32_t seg_idx){
	if(!wb) return;

	EPRINT("should I implement?", true);
	std::map<uint32_t, gc_mapping_check_node*>::iterator iter=gkv->begin();
	sst_file *now_check_sst=NULL;
	blockmanager *bm=LSM.pm->bm;

	key_ptr_pair *kp_set;
	while((kp_set=write_buffer_flush_for_gc(wb, false, seg_idx, NULL, UINT32_MAX, gkv))){
		free(kp_set);
	}

	bool remove_seg[10]={0,};
	gc_mapping_check_node *gmn;
	for(; iter!=gkv->end(); iter++){
		gmn=iter->second;
		if(gmn->type!=MAP_CHECK_FLUSHED_KP){
			EPRINT("it cannpt be", true);
		}

		if(now_check_sst){
			if(now_check_sst->start_lba <= gmn->lba && now_check_sst->end_lba>=gmn->lba){
				gmn->target_sst_file=now_check_sst;
				gmn->type=MAP_READ_DONE_PENDING;
				continue;
			}
		}

		now_check_sst=NULL;
		for(uint32_t i=0; i<LSM.param.LEVELN-1; i++){
			if(slm_invalidate_enable(i, gmn->piece_ppa)){
				now_check_sst=level_retrieve_sst(LSM.disk[i], gmn->lba);
				remove_seg[i]=true;
				gmn->level=i;
			}
		}

		if(!now_check_sst){
			EPRINT("wtf!!!", true);
		}

		gmn->type=MAP_READ_DONE;
		gmn->mapping_data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
		gmn->target_sst_file=now_check_sst;
		gmn->map_ppa=now_check_sst->file_addr.map_ppa;
		fdriver_lock_init(&gmn->done_lock, 0);
		gc_issue_mapcheck_read(gmn, bm->li);
	}

	for(uint32_t i=0; i<LSM.param.LEVELN-1; i++){
		if(remove_seg[i]){
			slm_remove_node(i, seg_idx);
		}
	}

	now_check_sst=NULL;
	gc_mapping_check_node *head_gmn=NULL;
	uint32_t max_piece_ppa=UINT32_MAX;
	char *now_data;
	for(iter=gkv->begin(); iter!=gkv->end(); ){
		gmn=iter->second;
		if(gmn->type==MAP_READ_DONE){
			if(head_gmn){
				sst_file_update_mapping(head_gmn->target_sst_file, head_gmn->mapping_data, gmn->level, 
						max_piece_ppa, LSM.pm);
				max_piece_ppa=UINT32_MAX;
				fdriver_destroy(&head_gmn->done_lock);
				free(head_gmn);
			}
			head_gmn=gmn;
			head_gmn->target_sst_file->data=head_gmn->mapping_data->value;
			now_data=head_gmn->mapping_data->value;
			fdriver_lock(&head_gmn->done_lock);
		}

		uint32_t idx=kp_find_idx(gmn->lba, now_data);
		if(((key_ptr_pair*)now_data)[idx].piece_ppa!=gmn->piece_ppa){
			EPRINT("invalid read", true);
		}	

		if(gmn->new_piece_ppa==UINT32_MAX){
			EPRINT("invalid ppa", true);
		}
		invalidate_piece_ppa(bm, gmn->piece_ppa, true);
		if(max_piece_ppa!=UINT32_MAX && SEGNUM(max_piece_ppa)!=SEGNUM(gmn->new_piece_ppa)){
			slm_coupling_level_seg(head_gmn->level, SEGNUM(max_piece_ppa), SEGPIECEOFFSET(max_piece_ppa), false);
			max_piece_ppa=UINT32_MAX;
		}

		if(max_piece_ppa==UINT32_MAX){
			max_piece_ppa=gmn->new_piece_ppa;
		}
		else{
			max_piece_ppa=max_piece_ppa<gmn->new_piece_ppa?gmn->new_piece_ppa:max_piece_ppa;
		}
		((key_ptr_pair*)now_data)[idx].piece_ppa=gmn->new_piece_ppa;

		if(gmn->type==MAP_READ_DONE){
			iter++;
			continue;
		}
		else{
			if(gmn->mapping_data){
				EPRINT("it should not be assigned memory", true);
			}
			free(gmn);
			gkv->erase(iter++);
		}
	}

	if(head_gmn){
		sst_file_update_mapping(head_gmn->target_sst_file, head_gmn->mapping_data, head_gmn->level,
				max_piece_ppa, LSM.pm);
		fdriver_destroy(&head_gmn->done_lock);
		free(head_gmn);
	}
}
