#include "page_manager.h"
#include "lsmtree.h"
#include "io.h"
extern lsmtree LSM;
extern uint32_t debug_piece_ppa;
extern uint32_t debug_lba;

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
	li->read(gmc->map_ppa, PAGESIZE, gmc->mapping_data,  req);
}

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
				gmn->sptr=now_check_sst;
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
		gmn->sptr=now_check_sst;
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
				sst_file_update_mapping(head_gmn->sptr, head_gmn->mapping_data, gmn->level, 
						max_piece_ppa, LSM.pm);
				max_piece_ppa=UINT32_MAX;
				fdriver_destroy(&head_gmn->done_lock);
				free(head_gmn);
			}
			head_gmn=gmn;
			head_gmn->sptr->data=head_gmn->mapping_data->value;
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
		invalidate_kp_entry(gmn->lba, gmn->piece_ppa, UINT32_MAX, true);
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
		sst_file_update_mapping(head_gmn->sptr, head_gmn->mapping_data, head_gmn->level,
				max_piece_ppa, LSM.pm);
		fdriver_destroy(&head_gmn->done_lock);
		free(head_gmn);
	}
}

void gc_helper_for_direct_mapping(std::map<uint32_t, gc_mapping_check_node*>*gkv, 
		struct write_buffer *wb, uint32_t seg_idx){
	if(!wb) return;

	if(LSM.flushed_kp_seg->find(seg_idx)==LSM.flushed_kp_seg->end()){
		EPRINT("not allowd", true);
	}
	
	key_ptr_pair *kp_set;
	while((kp_set=write_buffer_flush_for_gc(wb, false, seg_idx, NULL, UINT32_MAX, gkv))){
		free(kp_set);
	}

	LSM.flushed_kp_seg->erase(seg_idx);

	std::map<uint32_t, gc_mapping_check_node*>::iterator iter=gkv->begin();
	std::map<uint32_t, uint32_t>::iterator find_iter;
	for(; iter!=gkv->end(); ){
		/*flushed_kp_set*/
		find_iter=LSM.flushed_kp_set->find(iter->first);
		if(find_iter!=LSM.flushed_kp_set->end()){

#ifdef MIN_ENTRY_PER_SST
			if(LSM.unaligned_sst_file_set && LSM.unaligned_sst_file_set->now_sst_num){
				uint32_t idx=run_retrieve_sst_idx(LSM.unaligned_sst_file_set, find_iter->first);
				if(idx!=UINT32_MAX){
					invalidate_sst_file_map(&LSM.unaligned_sst_file_set->sst_set[idx]);
					run_remove_sst_file_at(LSM.unaligned_sst_file_set, idx);
					if(LSM.unaligned_sst_file_set->now_sst_num==0){
						run_free(LSM.unaligned_sst_file_set);
						LSM.unaligned_sst_file_set=NULL;
					}
				}
			}
#endif

			find_iter->second=iter->second->new_piece_ppa;
			LSM.flushed_kp_seg->insert(
					iter->second->new_piece_ppa/L2PGAP/_PPS);
			free(iter->second);
			gkv->erase(iter++);
			continue;
		}

		/*hot_kp_set*/
#ifdef WB_SEPARATE
		find_iter=LSM.hot_kp_set->find(iter->first);
		if(find_iter!=LSM.hot_kp_set->end()){
			find_iter->second=iter->second->new_piece_ppa;
			LSM.flushed_kp_seg->insert(
					iter->second->new_piece_ppa/L2PGAP/_PPS);
			free(iter->second);
			gkv->erase(iter++);
			continue;
		}
#endif
		/*flushed_kp_temp_set*/
		find_iter=LSM.flushed_kp_temp_set->find(iter->first);
		if(find_iter!=LSM.flushed_kp_temp_set->end()){
			find_iter->second=iter->second->new_piece_ppa;
			LSM.flushed_kp_seg->insert(
					iter->second->new_piece_ppa/L2PGAP/_PPS);
			free(iter->second);
			gkv->erase(iter++);
			continue;
		}

		iter++;
		abort();
	}
}

void *gc_helper_end_req(algo_req *req){
	gc_mapping_check_node *gcn;
	value_set *v;
	switch(req->type){
		case GCMR:
			gcn=(gc_mapping_check_node*)req->param;
			fdriver_unlock(&gcn->done_lock);
			break;
		case GCMW:
			v=(value_set*)req->param;
			inf_free_valueset(v, FS_MALLOC_R);
			break;
		default:
			EPRINT("not allowed type", true);
			break;
	}
	free(req);
	return NULL;
}

void gc_helper_issue_read_node(sst_file *sptr, gc_mapping_check_node *gcn, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	req->param=(void*)gcn;
	req->end_req=gc_helper_end_req;
	req->type=GCMR;
	gcn->mapping_data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	sptr->data=gcn->mapping_data->value;
	fdriver_lock_init(&gcn->done_lock, 0);
	if(gcn->is_issued_node){
		EPRINT("double issue", true);
	}
	gcn->is_issued_node=true;
	li->read(sptr->file_addr.map_ppa, PAGESIZE, gcn->mapping_data,  req);
}

void gc_helper_issue_write_node(uint32_t map_ppa, char *data, lower_info *li){
	algo_req *req=(algo_req*)malloc(sizeof(algo_req));
	value_set *value=inf_get_valueset(data, FS_MALLOC_W, PAGESIZE);
	req->param=(void*)value;
	req->end_req=gc_helper_end_req;
	req->type=GCMW;
	li->write(map_ppa, PAGESIZE, value,  req);
}

bool updating_compactioning_mapping(uint32_t seg_idx, uint32_t lba, uint32_t old_map_ppa, uint32_t new_map_ppa){
	map_range *mr;
	bool res=false;
	sst_file *file;
	if(LSM.compactioning_pos_set){
		for(uint32_t i=0; i<LSM.now_compaction_stream_num; i++){
			sst_pf_out_stream *pos=LSM.compactioning_pos_set[i];
			if(!pos) continue;

			if(pos->type==SST_PAGE_FILE_STREAM){			
				std::multimap<uint32_t, sst_file*>::iterator f_iter=
					pos->sst_map_for_gc->lower_bound(lba);

				if(f_iter!=pos->sst_map_for_gc->begin()){
					do{
						std::multimap<uint32_t, sst_file*>::iterator f_iter_temp=--f_iter;
						file=f_iter_temp->second;
						if(file->start_lba <=lba && file->end_lba >=lba){
							f_iter=f_iter_temp;
						}
						else{
							f_iter++;
							break;
						}
					}while(f_iter!=pos->sst_map_for_gc->begin());
				}

				for(;f_iter!=pos->sst_map_for_gc->end(); f_iter++){
					file=f_iter->second;
					if(file->end_lba < lba ) continue;
					if(file->start_lba > lba) break;
					if(file->file_addr.map_ppa==old_map_ppa){
						mr->ppa=new_map_ppa;
						res=true;
						goto next;
					}
				}
			}
			else{
				std::multimap<uint32_t, map_range*>::iterator m_iter;
				m_iter=pos->mr_map_for_gc->lower_bound(lba); 

				if(m_iter!=pos->mr_map_for_gc->begin()){
					do{
						std::multimap<uint32_t, map_range*>::iterator m_iter_temp=--m_iter;
						mr=m_iter_temp->second;
						if(mr->start_lba <= lba && mr->end_lba>=lba){
							m_iter=m_iter_temp;
						}
						else{
							m_iter++;
							break;
						}
					}while(m_iter!=pos->mr_map_for_gc->begin());
				}

				for(; m_iter!=pos->mr_map_for_gc->end(); m_iter++){
					mr=m_iter->second;
					if(mr->end_lba < lba) continue;
					if(mr->start_lba > lba) break;
					if(mr->ppa==old_map_ppa){
						mr->ppa=new_map_ppa;
						res=true;
						goto next;
					}
				}
			}
		}
	}

next:
	if(LSM.read_arg_set){
		for(uint32_t i=0; i<LSM.now_compaction_stream_num; i++){
			read_issue_arg *temp=LSM.read_arg_set[i];
			for(uint32_t j=temp->from; j<=temp->to; j++){
				if(temp->page_file==false){
					map_range *mr=&temp->map_target_for_gc[j];
					if(!mr) continue; //mr can be null, when it used all data.
					if(mr->start_lba > lba ||
							mr->end_lba < lba) continue;
					if(!mr->data && mr->read_done) continue;
					while(!mr->read_done){}
					if(mr->ppa==old_map_ppa){
						mr->ppa=new_map_ppa;
						res=true;
						goto out;
					}
				}
				else{
					if(temp->sst_target_for_gc[j]->start_lba > lba ||
							temp->sst_target_for_gc[j]->end_lba < lba) continue;
					if(temp->sst_target_for_gc[j]->file_addr.map_ppa==old_map_ppa){
						temp->sst_target_for_gc[j]->file_addr.map_ppa=new_map_ppa;
						res=true;
						goto out;
					}
				}
			}
		}
	}
out:
	return res;
}

void gc_helper_for_page_file(std::map<uint32_t, gc_mapping_check_node*>* gkv,
		std::multimap<uint32_t, gc_mapping_check_node*>* gm,
		std::multimap<uint32_t, gc_mapping_check_node*> *invalid_kp_data,
		struct write_buffer *wb, uint32_t seg_idx){
	key_ptr_pair *kp_set;
	gc_mapping_check_node *gcn;
	std::set<void *>sptr_set;
	std::map<uint32_t, gc_mapping_check_node*>::iterator gkv_iter=gkv->begin();
	uint32_t level_idx;
	uint32_t target_version;
	uint32_t sptr_idx;
	if(wb){
		gkv_iter=gkv->begin();
		while((kp_set=write_buffer_flush_for_gc(wb, false, seg_idx, NULL, UINT32_MAX, gkv))){
			for(uint32_t i=0; i<KP_IN_PAGE && kp_set[i].lba!=UINT32_MAX; i++, gkv_iter++){
				if(gkv_iter->first!=kp_set[i].lba){
					EPRINT("different kv!", true);
				}
				gcn=gkv_iter->second;
				if(gcn->lba==debug_lba){
					printf("break!\n");
				}
				gcn->sptr=lsmtree_find_target_normal_sst_datagc(gcn->lba, gcn->piece_ppa, &level_idx, &target_version, &sptr_idx);
				if(!gcn->sptr->data){
					gc_helper_issue_read_node(gcn->sptr, gcn, LSM.li);
				}
				sptr_set.insert((void*)gcn->sptr);
				updating_now_compactioning_data(gcn->version, seg_idx, gcn->lba, gcn->new_piece_ppa);
			}
			free(kp_set);
		}
	}

	gkv_iter=gm->begin();
	for(;gkv_iter!=gm->end(); gkv_iter++){
		gcn=gkv_iter->second;
		if(gcn->lba==36608){
			printf("break!\n");
		}
		gcn->sptr=lsmtree_find_target_normal_sst_datagc(gcn->lba, gcn->piece_ppa, &level_idx, &target_version, &sptr_idx);
		if(!gcn->sptr->data){
			gc_helper_issue_read_node(gcn->sptr, gcn, LSM.li);
		}
		sptr_set.insert((void*)gcn->sptr);
	}

	std::multimap<uint32_t, gc_mapping_check_node*>::iterator invalid_iter=invalid_kp_data->begin();
	for(; invalid_iter!=invalid_kp_data->end(); invalid_iter++){
		gcn=invalid_iter->second;
		gcn->sptr=lsmtree_find_target_normal_sst_datagc(gcn->lba, gcn->piece_ppa, &level_idx, &target_version, &sptr_idx);
		if(!gcn->sptr->data){
			gc_helper_issue_read_node(gcn->sptr, gcn, LSM.li);
		}
		sptr_set.insert((void*)gcn->sptr);	
	}


	gkv_iter=gkv->begin();
	for(;gkv_iter!=gkv->end(); gkv_iter++){
		gcn=gkv_iter->second;
		if(gcn->is_issued_node){
			fdriver_lock(&gcn->done_lock);
		}

		if(gcn->sptr->file_addr.map_ppa==debug_piece_ppa/L2PGAP){
			printf("break!\n");
		}
		sst_file *sptr=gcn->sptr;
		kp_set=(key_ptr_pair*)sptr->data;
		uint32_t kp_idx=kp_find_idx(gcn->lba, (char*)kp_set);
		kp_set[kp_idx].piece_ppa=gcn->new_piece_ppa;
		/*change readhelper ppa*/
		read_helper_update_ppa(sptr->_read_helper, kp_idx, gcn->new_piece_ppa);
	}

	invalid_iter=invalid_kp_data->begin();
	for(; invalid_iter!=invalid_kp_data->end(); invalid_iter++){
		gcn=invalid_iter->second;
		if(gcn->is_issued_node){
			fdriver_lock(&gcn->done_lock);
		}

		sst_file *sptr=gcn->sptr;
		kp_set=(key_ptr_pair*)sptr->data;
		uint32_t kp_idx=kp_find_idx(gcn->lba, (char*)kp_set);
		kp_set[kp_idx].piece_ppa=UINT32_MAX;
		/*change readhelper ppa*/
		read_helper_update_ppa(sptr->_read_helper, kp_idx, UINT32_MAX);
	}

	/*write all updated sstfile*/
	std::set<void*>::iterator sptr_iter=sptr_set.begin();
	for(; sptr_iter!=sptr_set.end(); sptr_iter++){
		sst_file *sptr=(sst_file*)(*sptr_iter);
		if(sptr->file_addr.map_ppa==debug_piece_ppa/L2PGAP){
			printf("break!\n");
		}
		uint32_t old_map_ppa=sptr->file_addr.map_ppa;
		invalidate_map_ppa(LSM.pm->bm, sptr->file_addr.map_ppa, true);
		uint32_t map_ppa=page_manager_get_reserve_new_ppa(LSM.pm, false, seg_idx);
		validate_map_ppa(LSM.pm->bm, map_ppa, sptr->start_lba, sptr->end_lba, true);	
		gc_helper_issue_write_node(map_ppa, sptr->data, LSM.li);
		sptr->file_addr.map_ppa=map_ppa;
	//	printf("updating mapping:%u~%u ppa:%u -> ppa:%u\n", sptr->start_lba, sptr->end_lba, old_map_ppa, map_ppa);
		updating_compactioning_mapping( seg_idx, gcn->lba, old_map_ppa, map_ppa);

		if(!sptr->compaction_used){
			sptr->data=NULL;
		}
	}

	gkv_iter=gkv->begin();
	for(;gkv_iter!=gkv->end(); gkv_iter++){
		gcn=gkv_iter->second;
		if(gcn->is_issued_node){
			inf_free_valueset(gcn->mapping_data, FS_MALLOC_R);
			fdriver_destroy(&gcn->done_lock);
		}
		free(gcn);
	}

	gkv_iter=gm->begin();
	for(;gkv_iter!=gm->end();gkv_iter++){
		gcn=gkv_iter->second;
		if(gcn->is_issued_node){
			inf_free_valueset(gcn->mapping_data, FS_MALLOC_R);
			fdriver_destroy(&gcn->done_lock);
		}
		free(gcn);
	}

	invalid_iter=invalid_kp_data->begin();
	for(; invalid_iter!=invalid_kp_data->end(); invalid_iter++){
		gcn=invalid_iter->second;
		if(gcn->is_issued_node){
			inf_free_valueset(gcn->mapping_data, FS_MALLOC_R);
			fdriver_destroy(&gcn->done_lock);
		}
		free(gcn);
	}
}
