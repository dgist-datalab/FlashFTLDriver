#include "compaction.h"
#include "lsmtree.h"
#include "segment_level_manager.h"
#include "io.h"
#include <math.h>
extern lsmtree LSM;
extern uint32_t debug_lba;
extern uint32_t debug_piece_ppa;

void* compaction_main(void *);
static volatile bool compaction_stop_flag;
compaction_master* compaction_init(uint32_t compaction_queue_num){
	compaction_master *cm=(compaction_master*)malloc(sizeof(compaction_master));
	cm->tm=tag_manager_init(compaction_queue_num);
	q_init(&cm->req_q, compaction_queue_num);

	cm->issue_worker=thpool_init(1);
	pthread_create(&cm->tid, NULL, compaction_main, (void*)cm);
	compaction_stop_flag=false;


	cm->read_param=(inter_read_alreq_param*)calloc(COMPACTION_TAGS, sizeof(inter_read_alreq_param));

	cm->read_param_queue=new std::queue<inter_read_alreq_param*>();
	for(uint32_t i=0; i<COMPACTION_TAGS; i++){
		cm->read_param_queue->push(&cm->read_param[i]);
	}
	//cm->kv_wrapper_slab=slab_master_init(sizeof(key_value_wrapper), COMPACTION_TAGS);
	return cm;
}

void compaction_free(compaction_master *cm){
	compaction_stop_flag=true;
	pthread_join(cm->tid, NULL);
	tag_manager_free_manager(cm->tm);

	free(cm->read_param);
	delete cm->read_param_queue;

	q_free(cm->req_q);
	free(cm);
}

void compaction_issue_req(compaction_master *cm, compaction_req *req){
	if(compaction_stop_flag) return;
	req->tag=tag_manager_get_tag(cm->tm);
	q_enqueue((void*)req, cm->req_q);
	return;
}

static inline void disk_change(level **up, level *src, level** des, uint32_t* idx_set){
	if(up!=NULL){
		level *new_up=level_init((*up)->max_sst_num, (*up)->max_run_num, (*up)->level_type, (*up)->idx, (*up)->max_contents_num, false);
		level_free(*up, LSM.pm);
		*up=new_up;
	}

	level *delete_target_level=*des;
	(*des)=src;
	level_free(delete_target_level, LSM.pm);
}

static inline void first_level_slm_coupling(key_ptr_pair *kp_set, bool is_gc_data){
	uint32_t end_idx=kp_end_idx((char*)kp_set);
	uint32_t prev_seg=UINT32_MAX;
	uint32_t prev_piece_ppa;

	if(SEGNUM(kp_set[0].piece_ppa)==SEGNUM(kp_set[end_idx].piece_ppa)){
		slm_coupling_level_seg(0, SEGNUM(kp_set[end_idx].piece_ppa), SEGPIECEOFFSET(kp_set[end_idx].piece_ppa), is_gc_data);
	}
	else{
		for(uint32_t i=0; i<=end_idx; i++){
			if(prev_seg==UINT32_MAX){
				prev_seg=SEGNUM(kp_set[i].piece_ppa);
				prev_piece_ppa=kp_set[i].piece_ppa;
				continue;
			}
			uint32_t now_seg=SEGNUM(kp_set[i].piece_ppa);
			if(now_seg==prev_seg){
				prev_piece_ppa=kp_set[i].piece_ppa;
			}
			else{
				slm_coupling_level_seg(0, prev_seg, SEGPIECEOFFSET(prev_piece_ppa), is_gc_data);
				prev_seg=now_seg;
				prev_piece_ppa=kp_set[i].piece_ppa;
			}
		}

		if(!slm_invalidate_enable(0, kp_set[end_idx].piece_ppa)){
			slm_coupling_level_seg(0, prev_seg, SEGPIECEOFFSET(prev_piece_ppa), is_gc_data);	
		}
	}
}

static inline bool check_sequential(std::map<uint32_t, uint32_t> *kp_set, write_buffer *wb){
	uint32_t start, end;
	if(kp_set->size()==0){
		start=wb->data->begin()->first;
		end=wb->data->rbegin()->first;	
		if(end-start+1==wb->buffered_entry_num) return true;
		else return false;
	}
	start=kp_set->begin()->first;
	end=kp_set->rbegin()->first;
	if(end-start+1==kp_set->size()){
		uint32_t prev_end=end;
		start=wb->data->begin()->first;
		end=wb->data->rbegin()->first;
		if(prev_end+1==start && 
			end-start+1==wb->buffered_entry_num){
			return true;	
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}
}
#if 0
//#ifdef MIN_ENTRY_OFF
extern char all_set_page[PAGESIZE];
static sst_file *kp_to_sstfile(std::map<uint32_t, uint32_t> *flushed_kp_set, 
		std::map<uint32_t, uint32_t>::iterator *temp_iter, bool make_rh){
	if(temp_iter && *temp_iter==flushed_kp_set->end()){
		return NULL;
	}
	value_set *vs=inf_get_valueset(all_set_page, FS_MALLOC_W, PAGESIZE);
	key_ptr_pair *kp_set=(key_ptr_pair*)vs->value;

	std::map<uint32_t, uint32_t>::iterator iter=temp_iter?*temp_iter:flushed_kp_set->begin();
	//uint32_t now_remain_page=page_manager_get_remain_page(LSM.pm, false);
	//uint32_t max_iter_cnt=MIN(KP_IN_PAGE, (now_remain_page-1)*L2PGAP);
	uint32_t max_iter_cnt=KP_IN_PAGE;
	//printf("max_iter_cnt:%u now_remain_page:%u\n", max_iter_cnt, now_remain_page);
	uint32_t i=0;
	read_helper *rh=NULL;

	uint32_t prev_seg_num=UINT32_MAX;
	uint32_t prev_piece_ppa=UINT32_MAX;
	bool sequential_file=true;

	std::map<uint32_t, uint32_t> reverse_map;
	for(; iter!=flushed_kp_set->end() && i<max_iter_cnt; i++, iter++ ){
		kp_set[i].lba=iter->first;
		kp_set[i].piece_ppa=iter->second;

		reverse_map.insert(std::pair<uint32_t, uint32_t>(kp_set[i].piece_ppa, kp_set[i].lba));
		/*
		if(make_rh){
			read_helper_stream_insert(rh, kp_set[i].lba, kp_set[i].piece_ppa);
		}*/
		slm_coupling_mem_lev_seg(SEGNUM(kp_set[i].piece_ppa), SEGPIECEOFFSET(kp_set[i].piece_ppa));
		if(sequential_file){
			if(prev_piece_ppa==UINT32_MAX){
				prev_piece_ppa=kp_set[i].piece_ppa;
			}
			else{
				if(!(prev_piece_ppa < kp_set[i].piece_ppa && prev_seg_num==SEGNUM(kp_set[i].piece_ppa))){
					printf("[not sequential:%u] prev:<%u %u,%u> now<%u %u,%u>\n",i, kp_set[i-1].lba, 
							kp_set[i-1].piece_ppa, SEGNUM(kp_set[i-1].piece_ppa),
							kp_set[i].lba, kp_set[i].piece_ppa, SEGNUM(kp_set[i].piece_ppa));
					sequential_file=false;
					continue;
				}
				prev_piece_ppa=kp_set[i].piece_ppa;
			}
			if(prev_seg_num==UINT32_MAX){
				prev_seg_num=SEGNUM(kp_set[i].piece_ppa);
			}
			else{
				if(prev_seg_num!=SEGNUM(kp_set[i].piece_ppa)){
					sequential_file=false;
					printf("prev_seg_num:%u now:%u\n", prev_seg_num, SEGNUM(kp_set[i].piece_ppa));
				}
				prev_seg_num=SEGNUM(kp_set[i].piece_ppa);
			}
		}
	}

	if(make_rh){
#ifdef DYNAMIC_HELPER_ASSIGN
		float density=(float)(i-1)/(kp_set[i-1].lba-kp_set[0].lba);
		if(density > LSM.param.BF_PLR_border && sequential_file){
			rh=read_helper_init(lsmtree_get_target_rhp(LSM.param.LEVELN-1));
		}
		else{
		#ifdef DYNAMIC_WISCKEY
			if(LSM.next_level_wisckey_compaction){
				rh=read_helper_init(LSM.param.bf_ptr_guard_rhp);
			}
			else{
				rh=read_helper_init(lsmtree_get_target_rhp(0));
			}
		#else
			rh=read_helper_init(lsmtree_get_target_rhp(0));
		#endif

		}
#else
		rh=read_helper_init(lsmtree_get_target_rhp(0));

#endif
		//read_helper
		for(uint32_t j=0; j<i; j++){
			read_helper_stream_insert(rh, kp_set[j].lba, kp_set[j].piece_ppa);
		}	
	}
	else{
		rh=NULL;
	}
	
	if(!sequential_file){
		uint32_t j=0;
		std::map<uint32_t, uint32_t>::iterator l_iter, p_iter;
		l_iter=temp_iter?*temp_iter:flushed_kp_set->begin();
		p_iter=reverse_map.begin();
		for(uint32_t j=0; j<i; j++, l_iter++, p_iter++){
			printf("<%u, %u> <%u, %u>\n", l_iter->first, l_iter->second, p_iter->first, p_iter->second);
		}
	}
	*temp_iter=iter;
	
	uint32_t last_idx=i-1;

	uint32_t map_ppa=page_manager_get_new_ppa(LSM.pm, false, DATASEG); //DATASEG for sequential tiering 
	validate_map_ppa(LSM.pm->bm, map_ppa, kp_set[0].lba,  kp_set[last_idx].lba, true);

	if(sequential_file){
		if(prev_seg_num==map_ppa/_PPS && prev_piece_ppa < map_ppa*L2PGAP){
			sequential_file=true;
		}
		else{
			sequential_file=false;
		}
	}

	sst_file *res=sst_init_empty(PAGE_FILE);
	res->file_addr.map_ppa=map_ppa;

	res->start_lba=kp_set[0].lba;
	res->end_lba=kp_set[last_idx].lba;
	res->_read_helper=rh;
	res->start_piece_ppa=kp_set[0].piece_ppa;
	res->end_ppa=UINT32_MAX;

	res->sequential_file=sequential_file;
	/*
	if(SEGNUM(kp_set[0].piece_ppa)==SEGNUM(kp_set[last_idx].piece_ppa) && 
			SEGNUM(kp_set[0].piece_ppa)==map_ppa/_PPS){
		res->sequential_file=true;
	}*/

	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=MAPPINGW;
	write_req->param=(void*)vs;
	write_req->end_req=comp_alreq_end_req;
	io_manager_issue_internal_write(map_ppa, vs, write_req, false);

	return res;
}

static inline void flush_and_move(std::map<uint32_t, uint32_t> *kp_set, 
		std::map<uint32_t, uint32_t> * hot_kp_set,
		write_buffer *wb, uint32_t flushing_target_num){
	key_ptr_pair *temp_kp_set=write_buffer_flush(wb, flushing_target_num, false);
	bool test_flag=kp_set==LSM.flushed_kp_temp_set?true:false;
//	static uint32_t cnt=0;
//	printf("[%u]temp_kp_set first:<%u %u,%u>\n", cnt++,temp_kp_set[0].lba, temp_kp_set[0].piece_ppa, SEGNUM(temp_kp_set[0].piece_ppa));
	for(uint32_t i=0; i<KP_IN_PAGE && temp_kp_set[i].lba!=UINT32_MAX; i++){
		LSM.flushed_kp_seg->insert(temp_kp_set[i].piece_ppa/L2PGAP/_PPS);
		if(test_flag && temp_kp_set[i].piece_ppa==debug_piece_ppa){
			printf("%u target is moved to temp_ppa\n", temp_kp_set[i].piece_ppa);
		}
		std::map<uint32_t, uint32_t>::iterator find_iter;
		if(hot_kp_set){
			find_iter=hot_kp_set->find(temp_kp_set[i].lba);
			if(find_iter!=hot_kp_set->end()){ //hit in hot_kp_set
				invalidate_kp_entry(find_iter->first, find_iter->second, UINT32_MAX, true);
				hot_kp_set->erase(find_iter);
				hot_kp_set->insert(
						std::pair<uint32_t, uint32_t>(temp_kp_set[i].lba, temp_kp_set[i].piece_ppa));
				continue;
			}
		}

		find_iter=kp_set->find(temp_kp_set[i].lba);
		if(find_iter!=kp_set->end()){ //move hot_kp_set
#ifdef LSM_DEBUG
			if(debug_lba==find_iter->first){
				printf("target hit in mem level: %u, %u\n", find_iter->first, find_iter->second);
			}
	//		printf("%u hit!\n", find_iter->first);
#endif
			invalidate_kp_entry(find_iter->first, find_iter->second, UINT32_MAX, true);
			kp_set->erase(find_iter);
			if(hot_kp_set){
				if(debug_piece_ppa==temp_kp_set[i].piece_ppa){
					printf("%u is moved to hot set\n", debug_piece_ppa);
				}
				hot_kp_set->insert(
						std::pair<uint32_t, uint32_t>(temp_kp_set[i].lba, temp_kp_set[i].piece_ppa));
				continue;
			}
		}

		kp_set->insert(
				std::pair<uint32_t, uint32_t>(temp_kp_set[i].lba, temp_kp_set[i].piece_ppa));	
		version_coupling_lba_version(LSM.last_run_version, temp_kp_set[i].lba, UINT8_MAX);
	}
	free(temp_kp_set);
}

level *make_pinned_level(std::map<uint32_t, uint32_t> * kp_set){
	level *res=level_init(kp_set->size()/KP_IN_PAGE+(kp_set->size()%KP_IN_PAGE?1:0),1, LEVELING_WISCKEY, UINT32_MAX, kp_set->size(), false);
	sst_file *sptr=NULL;
	std::map<uint32_t, uint32_t>::iterator iter=kp_set->begin();
	while((sptr=kp_to_sstfile(kp_set, &iter, true))){
		level_append_sstfile(res, sptr, true);
		sst_free(sptr, LSM.pm);
	}
	return res;
}
level* flush_memtable(write_buffer *wb, bool is_gc_data){
	if(page_manager_get_total_remain_page(LSM.pm, false, false) < MAX(wb->buffered_entry_num/L2PGAP,2)){
		__do_gc(LSM.pm, false, KP_IN_PAGE/L2PGAP);
	}
	
	rwlock_write_lock(&LSM.flush_wait_wb_lock);
	rwlock_write_lock(&LSM.flushed_kp_set_lock);
	if(LSM.flushed_kp_set==NULL){
		LSM.flushed_kp_set=new std::map<uint32_t, uint32_t>();
		LSM.flushed_kp_temp_set=new std::map<uint32_t, uint32_t>();
#ifdef WB_SEPARATE
		LSM.hot_kp_set=new std::map<uint32_t, uint32_t>();
#endif
	}
	level *res=NULL;

	bool is_sequential=check_sequential(LSM.flushed_kp_set, wb);
	uint32_t now_remain_page_num=UINT32_MAX;
	uint32_t flushing_entry_num=KP_IN_PAGE;
	bool making_level=false;
	if(is_sequential){
		now_remain_page_num=page_manager_get_remain_page(LSM.pm, false);
		if(now_remain_page_num<2){
			now_remain_page_num=page_aligning_data_segment(LSM.pm, 2);
		}
		uint32_t mapping_num=CEILING_TARGET(LSM.flushed_kp_set->size(), KP_IN_PAGE);
		uint32_t now_flushing_num=CEILING_TARGET(wb->buffered_entry_num, L2PGAP);
		uint32_t update_mapping_num=CEILING_TARGET(wb->buffered_entry_num+LSM.flushed_kp_set->size(), KP_IN_PAGE);

		if(now_remain_page_num < (now_flushing_num+update_mapping_num)){
			if(now_remain_page_num < mapping_num){
				EPRINT("sequential_error", true);
			}

			uint32_t remain_space=now_remain_page_num-mapping_num;
			uint32_t mapping_remain_space=LSM.flushed_kp_set->size()%KP_IN_PAGE;

			for(uint32_t i=1; i<=wb->buffered_entry_num; i++){
				uint32_t needed_data_page=CEILING_TARGET(i, L2PGAP);
				uint32_t needed_map_page=(int)(i-mapping_remain_space) <0 ? 0: CEILING_TARGET(i-mapping_remain_space, KP_IN_PAGE);
				if(needed_data_page+needed_map_page<=remain_space) continue;
				else{
					flushing_entry_num=i-1;
					break;
				}
			}
			making_level=true;
		}

		if(flushing_entry_num==0){
			res=make_pinned_level(LSM.flushed_kp_set); //aligned previous pinned_level
			making_level=false;
		}
		else{
			flush_and_move(LSM.flushed_kp_set,
#ifdef WB_SEPARATE
					LSM.hot_kp_set,
#else
					NULL,
#endif
					wb, flushing_entry_num); //inset data to remain space
		}

		if(making_level || (!res && LSM.flushed_kp_set->size()>=LSM.param.write_buffer_ent)){
			if(res){
				EPRINT("what happen?, 'res' should be NULL", true);
			}
			res=make_pinned_level(LSM.flushed_kp_set); //aligned pinned_level
		}
	
		/*insert remain entry to flushed_temp_kp_set*/
		if(wb->buffered_entry_num){
			flush_and_move(LSM.flushed_kp_temp_set,
#ifdef WB_SEPARATE
					LSM.hot_kp_set,
#else
					NULL,
#endif
					wb, wb->buffered_entry_num); //inset data to remain space
		}	
	}
	else{
		flush_and_move(LSM.flushed_kp_set,
#ifdef WB_SEPARATE
				LSM.hot_kp_set,
#else
				NULL,
#endif
				wb, wb->buffered_entry_num); //inset data to remain space

		if(LSM.flushed_kp_set->size()+LSM.flushed_kp_temp_set->size()+
#ifdef WB_SEPARATE
				LSM.hot_kp_set->size()
#else
				0
#endif
				>= LSM.param.write_buffer_ent){
			res=make_pinned_level(LSM.flushed_kp_set);
		}
	}

	write_buffer_free(wb);
	LSM.flush_wait_wb=NULL;
	rwlock_write_unlock(&LSM.flushed_kp_set_lock);
	rwlock_write_unlock(&LSM.flush_wait_wb_lock);

	return res;
}
#endif

static inline void managing_remain_space_for_wisckey(level *src, level *des){
	uint32_t needed_map_page_num=src->now_sst_num+(des?des->now_sst_num:0);
	if(page_manager_get_total_remain_page(LSM.pm, true, false) < needed_map_page_num){
		__do_gc(LSM.pm, true, needed_map_page_num);	
	}
}

static inline level *TW_compaction(compaction_master *cm, level *src, level *des,
		uint32_t target_version, bool *populated){
	level *res=NULL;
	managing_remain_space_for_wisckey(src, NULL);
	level *temp=compaction_TW_convert_LW(cm, src);
	switch(des->level_type){
		case LEVELING:
			res=compaction_LW2LE(cm, temp, des, target_version);
			break;
		case LEVELING_WISCKEY:
			managing_remain_space_for_wisckey(temp, des);
			res=compaction_LW2LW(cm, temp, des, target_version);
			break;
		case TIERING:
			res=compaction_LW2TI(cm, temp, des, target_version, populated);
			break;
		case TIERING_WISCKEY:
			res=compaction_LW2TW(cm, temp, des, target_version, populated);
			break;
	}
	level_free(temp, LSM.pm);
	return res;
}
char rw_debug_flag=0;
static inline void do_compaction_demote(compaction_master *cm, compaction_req *req, 
		level *temp_level, uint32_t start_idx, uint32_t end_idx){

	level *lev=NULL;
	static uint32_t demote_cnt=0;
	printf("demote_cnt: %u\n", demote_cnt++);
	if(temp_level){
#ifdef DYNAMIC_WISCKEY
		temp_level->compacting_wisckey_flag=LSM.next_level_wisckey_compaction;
#endif
		LSM.monitor.flushed_kp_num+=LSM.flushed_kp_set->size();
		rwlock_write_lock(&LSM.level_rwlock[end_idx]);
		for(std::set<uint32_t>::iterator iter=LSM.flushed_kp_seg->begin(); 
				iter!=LSM.flushed_kp_seg->end(); iter++){
			lsmtree_gc_unavailable_set(&LSM, NULL, *iter);
		}
	}else{
		rwlock_write_lock(&LSM.level_rwlock[end_idx]);
		rwlock_write_lock(&LSM.level_rwlock[start_idx]);
	}

	uint32_t end_type=LSM.disk[end_idx]->level_type;
	level *src_lev=temp_level?temp_level:LSM.disk[start_idx];
	level *des_lev=LSM.disk[end_idx];

	if(des_lev->run_num!=LSM.last_run_version->version_populate_queue[des_lev->idx][VERSION]->size()){
		EPRINT("What happend?", true);
	}

	uint32_t target_version;
	bool populated=true;
	if(des_lev->level_type==LEVELING || des_lev->level_type==LEVELING_WISCKEY){
		target_version=version_order_to_version(LSM.last_run_version, des_lev->idx, 0);
	}
	else{	
		target_version=version_get_empty_version(LSM.last_run_version, des_lev->idx);
	}

	LSM.monitor.compaction_cnt[end_idx]++;
#ifdef HOT_COLD
	bool hot_cold_separation_compaction=false;
#endif


	switch(src_lev->level_type){
		case LEVELING:
			if(end_type==LEVELING){
				lev=compaction_LE2LE(cm, src_lev, des_lev, target_version);
			}
			else{
				lev=compaction_LE2TI(cm, src_lev, des_lev, target_version, &populated);
			}
			break;
		case LEVELING_WISCKEY:
			switch(end_type){
				case LEVELING_WISCKEY:
					managing_remain_space_for_wisckey(src_lev, des_lev);
					lev=compaction_LW2LW(cm, src_lev, des_lev, target_version);
					break;
				case LEVELING:
					lev=compaction_LW2LE(cm, src_lev, des_lev, target_version);
					break;
				case TIERING_WISCKEY:
					/*managing_remain_space_for_wisckey(src_lev, des_lev); no need to write data*/
					lev=compaction_LW2TW(cm, src_lev, des_lev, target_version, &populated);
					break;
				case TIERING:
#ifdef DYNAMIC_WISCKEY
					if(src_lev->compacting_wisckey_flag){
						lev=compaction_LW2TW(cm, src_lev, des_lev, target_version, &populated);
					}
					else{
#endif
						lev=compaction_LW2TI(cm, src_lev, des_lev, target_version, &populated);
					
#ifdef DYNAMIC_WISCKEY
					}
#endif
					break;
			}
			break;
		case TIERING:
#ifdef HOT_COLD
			lev=compaction_TI2TI_separation(cm ,src_lev, des_lev, target_version, 
					&hot_cold_separation_compaction, &populated);
#else
			lev=compaction_TI2TI(cm, src_lev, des_lev, target_version, &populated);
#endif
			break;
		case TIERING_WISCKEY:
			lev=TW_compaction(cm, src_lev, des_lev, target_version, &populated);
			break;
	}

	/*populate version*/
	if(des_lev->level_type==LEVELING || des_lev->level_type==LEVELING_WISCKEY){
		if(!LSM.last_run_version->version_populate_queue[des_lev->idx][VERSION]->size()){
			if(populated){
				version_populate(LSM.last_run_version, target_version, des_lev->idx);
			}
			else{
				version_reunpopulate(LSM.last_run_version, target_version, des_lev->idx);	
			}
		}
	}
	else{
		if(populated){
			version_populate(LSM.last_run_version, target_version, des_lev->idx);
		}
		else{
			version_reunpopulate(LSM.last_run_version, target_version, des_lev->idx);	
		}
	}

	/*unpopulate_version*/
	if(!temp_level){
		if(src_lev->level_type==LEVELING || src_lev->level_type==LEVELING_WISCKEY){
			version_clear_level(LSM.last_run_version, src_lev->idx);
		}
		else{
			/*matching order*/
#ifdef HOT_COLD
			if(hot_cold_separation_compaction){
				/*do nothing*/
			}
			else{
				version_clear_level(LSM.last_run_version, src_lev->idx);
			}
#else
			version_clear_level(LSM.last_run_version, src_lev->idx);
#endif
		}
	}

#ifdef HOT_COLD
	if(hot_cold_separation_compaction){
		disk_change(NULL, lev, &LSM.disk[end_idx], NULL);
	}
	else{
		disk_change(temp_level?NULL:&LSM.disk[start_idx], lev, &LSM.disk[end_idx], NULL);
	}
#else
	disk_change(temp_level?NULL:&LSM.disk[start_idx], lev, &LSM.disk[end_idx], NULL);
#endif

	if(end_idx==LSM.param.LEVELN-1){
		slm_empty_level(start_idx);
	}
	else if(temp_level){
		slm_move_mem_lev_seg(end_idx);
	}
	else{
		slm_move(end_idx, start_idx);
	}

	if(temp_level){
		rwlock_write_unlock(&LSM.level_rwlock[end_idx]);
		rwlock_write_lock(&LSM.flushed_kp_set_lock);

		for(std::set<uint32_t>::iterator iter=LSM.flushed_kp_seg->begin(); 
				iter!=LSM.flushed_kp_seg->end(); iter++){
			lsmtree_gc_unavailable_unset(&LSM, NULL, *iter);
		}
		LSM.flushed_kp_seg->clear();
	
		/*coupling version*/
		std::map<uint32_t, uint32_t>::iterator map_iter;
		for(map_iter=LSM.flushed_kp_set->begin(); map_iter!=LSM.flushed_kp_set->end(); map_iter++){
			version_coupling_lba_version(LSM.last_run_version, map_iter->first, target_version);
		}
		delete LSM.flushed_kp_set;
		LSM.flushed_kp_set=NULL;

		if(LSM.flushed_kp_temp_set->size() || 
#ifdef WB_SEPARATE
				LSM.hot_kp_set->size()
#else
				false
#endif
				){
			std::map<uint32_t, uint32_t>::iterator iter;
			LSM.flushed_kp_set=new std::map<uint32_t, uint32_t>();
#ifdef WB_SEPARATE
			for(iter=LSM.hot_kp_set->begin(); iter!=LSM.hot_kp_set->end();){
				LSM.flushed_kp_seg->insert(iter->second/L2PGAP/_PPS);
				LSM.flushed_kp_set->insert(std::pair<uint32_t,uint32_t>(iter->first, iter->second));
				LSM.hot_kp_set->erase(iter++);
			}
			LSM.hot_kp_set->clear();
#endif
		
			for(iter=LSM.flushed_kp_temp_set->begin(); iter!=LSM.flushed_kp_temp_set->end();){
				LSM.flushed_kp_seg->insert(iter->second/L2PGAP/_PPS);
				LSM.flushed_kp_set->insert(std::pair<uint32_t,uint32_t>(iter->first, iter->second));
				LSM.flushed_kp_temp_set->erase(iter++);
			}
			LSM.flushed_kp_temp_set->clear();
		}
		else{
			delete LSM.flushed_kp_temp_set;
#ifdef WB_SEPARATE
			delete LSM.hot_kp_set;
			LSM.hot_kp_set=NULL;
#endif
			LSM.flushed_kp_temp_set=NULL;
			LSM.flushed_kp_set=NULL;
		}
		level_free(temp_level, LSM.pm);
		LSM.pinned_level=NULL;
		rwlock_write_unlock(&LSM.flushed_kp_set_lock);
#ifdef DYNAMIC_WISCKEY
		LSM.next_level_wisckey_compaction=lsmtree_target_run_wisckeyable(LSM.param.write_buffer_ent, true);
#endif
	}
	else{
		version_level_invalidate_number_init(LSM.last_run_version, start_idx);
		rwlock_write_unlock(&LSM.level_rwlock[end_idx]);
		rwlock_write_unlock(&LSM.level_rwlock[start_idx]);
	}

	lsmtree_gc_unavailable_sanity_check(&LSM);
	version_sanity_checker(LSM.last_run_version);
}

static inline void do_compaction_inplace(compaction_master *cm, compaction_req *req, 
		uint32_t start_idx, uint32_t end_idx){
#ifdef LSM_DEBUG
	static int inplace_cnt=0;
	printf("inplace[%d] %u %u\n", inplace_cnt++, start_idx, end_idx);
#endif
	rwlock_write_lock(&LSM.level_rwlock[start_idx]);
	level *src_lev=LSM.disk[start_idx];
	level *des_lev=LSM.disk[end_idx];
	level *res;

	bool issequential;
	uint32_t target_version=version_order_to_version(LSM.last_run_version, start_idx, 0);
	run **target_run=compaction_TI2RUN(cm, src_lev, des_lev, 
			src_lev->run_num, target_version, UINT32_MAX, &issequential, true, false);

	if(src_lev->run_num < src_lev->max_run_num){
		version_get_resort_version(LSM.last_run_version, src_lev->idx);
	}
	else{
		version_clear_level(LSM.last_run_version, src_lev->idx);
	}

	target_version=version_get_empty_version(LSM.last_run_version, src_lev->idx);
	LSM.monitor.compaction_cnt[start_idx]++;

	res=level_init(src_lev->max_sst_num, src_lev->max_run_num, src_lev->level_type, 
			src_lev->idx, src_lev->max_contents_num, true);
	version_populate(LSM.last_run_version, target_version, src_lev->idx);

	uint32_t target_ridx=version_to_ridx(LSM.last_run_version,  src_lev->idx, target_version);
	level_update_run_at_move_originality(res, target_ridx, target_run[0], true);

	run_free(target_run[0]);
	if(!issequential){
		free(target_run);
	}

	level_free(src_lev, LSM.pm);
	LSM.disk[start_idx]=res;
	rwlock_write_unlock(&LSM.level_rwlock[start_idx]);

	lsmtree_gc_unavailable_sanity_check(&LSM);
	version_sanity_checker(LSM.last_run_version);
}

static inline void do_compaction(compaction_master *cm, compaction_req *req, 
		level *temp_level, uint32_t start_idx, uint32_t end_idx){
	if(temp_level){
		do_compaction_demote(cm, req, temp_level, start_idx, end_idx);
		return;
	}

	level *src_lev=LSM.disk[start_idx];
	if(src_lev->level_type!=TIERING || start_idx==0){
		do_compaction_demote(cm, req, temp_level, start_idx, end_idx);
		return;
	}

	level *prev_lev=LSM.disk[start_idx-1];
	uint32_t now_invalidate_num=version_get_level_invalidation_cnt(LSM.last_run_version, start_idx);
	
	if(now_invalidate_num >= prev_lev->max_contents_num){ //the level is physically full but the capacity of invalidated data exceeds upper level capacity
		do_compaction_inplace(cm, req, start_idx, end_idx);
	}
	else if(!(src_lev->run_num<src_lev->max_run_num) && 
			(src_lev->now_contents_num+src_lev->max_contents_num/LSM.param.normal_size_factor < prev_lev->max_contents_num)){ 
			//the level is logically full but it has enough space to store upper level's data
		do_compaction_inplace(cm, req, start_idx, end_idx);
	}
	else{//level is physically full
		do_compaction_demote(cm, req, temp_level, start_idx, end_idx);
	}
}

static inline level* do_reclaim_level(compaction_master *cm, level *target_lev){
	if(target_lev->level_type!=TIERING){
		EPRINT("cannot do GC", true);
	}
	level *res;
	uint32_t merged_idx_set[MERGED_RUN_NUM];
	version_get_merge_target(LSM.last_run_version, merged_idx_set, LSM.param.LEVELN-1);
	res=compaction_merge(cm, target_lev, merged_idx_set);

	disk_change(NULL, res, &LSM.disk[res->idx], merged_idx_set);
	target_lev=res;

	version_invalidate_number_init(LSM.last_run_version, merged_idx_set[0]);
	version_invalidate_number_init(LSM.last_run_version, merged_idx_set[1]);

	if(page_manager_get_total_remain_page(LSM.pm, false, true) > LSM.param.reclaim_ppa_target){
		printf("target:%u now_remain:%u\n", LSM.param.reclaim_ppa_target, page_manager_get_total_remain_page(LSM.pm, false, true));
		return res;
	}
	return res;

	/*
//	uint32_t round=0;
//	printf("%u target:%u now_remain:%u\n", round++, LSM.param.reclaim_ppa_target, page_manager_get_total_remain_page(LSM.pm, false, true));
//
//	std::multimap<uint32_t, uint32_t> version_inv_map;
//	uint32_t start_ver=version_level_to_start_version(LSM.last_run_version, target_lev->idx);
//	uint32_t end_ver=version_level_to_start_version(LSM.last_run_version, target_lev->idx-1)-1;
//	for(uint32_t i=start_ver; i<=end_ver;i++ ){
//		version_inv_map.insert(
//				std::pair<uint32_t, uint32_t>(LSM.last_run_version->version_invalidate_number[i], i));
//	}
//
//	std::multimap<uint32_t, uint32_t>::reverse_iterator iter=version_inv_map.rbegin();
//	for(; iter!=version_inv_map.rend(); iter++){
//		uint32_t ridx=version_to_ridx(LSM.last_run_version, target_lev->idx, iter->second);
//		printf("target v:%u inv:%u\n", iter->second, iter->first);
//		if(ridx==merged_idx_set[0] || iter->first==0) continue;
//		run *rptr=LEVEL_RUN_AT_PTR(res, ridx);
//		if(rptr->now_sst_num){
//			LSM.monitor.compaction_reclaim_run_num++;
//			run *new_run=compaction_reclaim_run(cm, rptr, ridx);
//			run_empty_content(rptr, LSM.pm);
//
//			uint32_t t_version=iter->second;
//			version_invalidate_number_init(LSM.last_run_version, t_version);
//			level_update_run_at_move_originality(res, ridx, new_run, false);
//			run_free(new_run);
//#ifdef LSM_DEBUG
//			printf("%u target:%u now_remain:%u\n", round++, LSM.param.reclaim_ppa_target, page_manager_get_total_remain_page(LSM.pm, false, true));
//#endif
//
//			if(page_manager_get_total_remain_page(LSM.pm, false, true) > LSM.param.reclaim_ppa_target){
//				break;
//			}
//		}
//	}
//	version_inv_map.clear();
//	run *rptr;
//	uint32_t ridx, cnt;
//	uint32_t round=0;
//	for_each_old_ridx_in_lev(LSM.last_run_version, ridx, cnt, target_lev->idx){
//		if(ridx==merged_idx_set[0]) continue;
//		rptr=LEVEL_RUN_AT_PTR(res, ridx);
//		if(rptr->now_sst_num){
//			LSM.monitor.compaction_reclaim_run_num++;
//			run *new_run=compaction_reclaim_run(cm, rptr, ridx);
//			run_empty_content(rptr, LSM.pm);
//
//			uint32_t t_version=version_ridx_to_version(LSM.last_run_version, target_lev->idx, ridx);
//
//			level_update_run_at_move_originality(res, ridx, new_run, false);
//			run_free(new_run);
//		}
//		if(page_manager_get_total_remain_page(LSM.pm, false, true) > LSM.param.reclaim_ppa_target){
//			break;
//		}
//		else{
//			printf("%u target:%u now_remain:%u\n", round++, LSM.param.reclaim_ppa_target, page_manager_get_total_remain_page(LSM.pm, false, true));
//		}
//	}
*/
	return res;
}

static void last_level_reclaim(compaction_master *cm, uint32_t level_idx){
	level *lev=LSM.disk[level_idx];
	if(lev->level_type!=TIERING) return;

	rwlock_write_lock(&LSM.level_rwlock[level_idx]);

	do_reclaim_level(cm, LSM.disk[level_idx]);

	LSM.monitor.compaction_cnt[level_idx+1]++;
	rwlock_write_unlock(&LSM.level_rwlock[level_idx]);
}

void check_and_make_available_data(){
	for(std::set<uint32_t>::iterator iter=LSM.flushed_kp_seg->begin(); 
			iter!=LSM.flushed_kp_seg->end(); iter++){
		lsmtree_gc_unavailable_set(&LSM, NULL, *iter);
	}
	if(page_manager_get_total_remain_page(LSM.pm, false, true) <_PPS){
		/*do somthing*/
	}
	for(std::set<uint32_t>::iterator iter=LSM.flushed_kp_seg->begin(); 
			iter!=LSM.flushed_kp_seg->end(); iter++){
		lsmtree_gc_unavailable_unset(&LSM, NULL, *iter);
	}
}

bool force_merging_test(){
	uint32_t total_remain_page=page_manager_get_total_remain_page(LSM.pm, true, false);
	if(total_remain_page>_PPS) return false;
	uint32_t total_invalidate_number=lsmtree_get_seg_invalidate_number();
	if(total_invalidate_number<_PPS){
		printf("force_merging!\n");
	}
	return total_invalidate_number < _PPS;
}

void* compaction_main(void *_cm){
	compaction_master *cm=(compaction_master*)_cm;
	queue *req_q=(queue*)cm->req_q;
	uint32_t above_sst_num;
	bool force=false;
#ifdef LSM_DEBUG
	uint32_t contents_num;
#endif
	while(!compaction_stop_flag){
		compaction_req *req=(compaction_req*)q_dequeue(req_q);
		if(!req) continue;
again:
#ifdef DEMAND_SEG_LOCK	
		lsmtree_unblock_already_gc_seg(&LSM);
#endif
		level *temp_level=NULL;
		if(req->start_level==-1 && req->end_level==0){
			if(req->wb){
				/*check and make available data area*/
		//		check_and_make_available_data();
				temp_level=flush_memtable(req->wb, false);
			}
			else{
				EPRINT("wtf", true);
			}
		}

		if(req->start_level==-1 && !temp_level){
			goto end;
		}

		if(force && req->start_level==LSM.param.LEVELN-1){
			if(level_is_full(LSM.disk[req->start_level], LSM.param.last_size_factor)){
				last_level_reclaim(cm, LSM.param.LEVELN-1);
			}
			goto end;
		}

		static uint32_t temp_level_cnt=0;
		if(temp_level){
			uint32_t remain_page=page_manager_get_total_remain_page(LSM.pm, false, true);
			uint32_t needed_page_num=LSM.flushed_kp_set->size()/L2PGAP+1+
				temp_level->now_sst_num;
			if(remain_page < needed_page_num){
				if(__do_gc(LSM.pm, false, needed_page_num)){
					sst_file *sptr;
					run *rptr;
					uint32_t sidx, ridx;
					for_each_sst_level(temp_level, rptr, ridx, sptr, sidx){
						if(sptr->type==BLOCK_FILE){
							map_range *mptr;
							uint32_t midx;
							for_each_map_range(sptr, mptr, midx){
								invalidate_map_ppa(LSM.pm->bm, mptr->ppa, true);
							}
						}
						else{
							invalidate_map_ppa(LSM.pm->bm, sptr->file_addr.map_ppa, true);
						}
					}

					level_free(temp_level, LSM.pm);
					LSM.pinned_level=NULL;
					temp_level=make_pinned_level(LSM.flushed_kp_set);
				}
			}
		}

		do_compaction(cm, req, temp_level, req->start_level, req->end_level);

		lsmtree_level_summary(&LSM, false);

		if(level_is_full(LSM.disk[req->end_level], LSM.param.last_size_factor)){
			if((req->end_level==LSM.param.LEVELN-2 && level_is_full(LSM.disk[LSM.param.LEVELN-1], LSM.param.last_size_factor))){
				last_level_reclaim(cm, LSM.param.LEVELN-1);
			}
			else if(req->end_level==LSM.param.LEVELN-1){
				goto end;
			}
			
			req->start_level=req->end_level;
			req->end_level++;
			goto again;
		}
		else if(LSM.disk[req->end_level]->level_type==LEVELING || 
				LSM.disk[req->end_level]->level_type==LEVELING_WISCKEY){
			/*this path will test leveling level*/
			above_sst_num=req->end_level==0? 
				temp_level->max_sst_num:
				LSM.disk[req->end_level-1]->max_sst_num; 
			if(!level_is_appendable(LSM.disk[req->end_level], above_sst_num)){
				if(req->end_level==LSM.param.LEVELN-2 && level_is_full(LSM.disk[LSM.param.LEVELN-1], LSM.param.last_size_factor)){
					last_level_reclaim(cm, LSM.param.LEVELN-1);
#ifdef LSM_DEBUG
					contents_num=get_level_content_num(LSM.disk[LSM.param.LEVELN-1]);
					printf("L%u %u %.2f\n", LSM.param.LEVELN-1, contents_num, (float)contents_num*100/RANGE);
#endif
				}
				req->start_level=req->end_level;
				req->end_level++;
				goto again;
			}
		}
		/*
		if(force_merging_test()){
			last_level_reclaim(cm, LSM.param.LEVELN-1);
		}*/
end:
		if(force==false && page_manager_get_total_remain_page(LSM.pm, false, true) <_PPS){
			uint32_t res=lsmtree_total_invalidate_num(&LSM);
			if(res<_PPS*L2PGAP*4){
				
				static int cnt=0;
				printf("force compaction! %u\n", cnt++);
				if(cnt==18){
					printf("break!\n");
					LSM.global_debug_flag=true;
				}
				lsmtree_level_summary(&LSM, false);
				if((req->end_level==LSM.param.LEVELN-2 && level_is_full(LSM.disk[LSM.param.LEVELN-1], LSM.param.last_size_factor))){
					last_level_reclaim(cm, LSM.param.LEVELN-1);
	//				goto end;
				}
				else{
					float min_validate_ratio=1.0f;
					float target_validate_ratio;
					uint32_t target_version=0;
					run *target_run=NULL;
					uint32_t target_ridx;
					uint32_t ridx;
					run *rptr;
					for_each_run_max(LSM.disk[LSM.param.LEVELN-1], rptr, ridx){
						if(!rptr->now_sst_num) continue;
						uint32_t version_number=version_ridx_to_version(LSM.last_run_version, LSM.param.LEVELN-1, ridx);
						if(LSM.last_run_version->version_invalidate_number[version_number]==0) continue;
						target_validate_ratio=(float)(rptr->now_contents_num - LSM.last_run_version->version_invalidate_number[version_number])/rptr->now_contents_num;
						if(min_validate_ratio > target_validate_ratio){
							min_validate_ratio=target_validate_ratio;
							target_run=rptr;
							target_version=version_number;
							target_ridx=ridx;
						}
					}
					if(target_run){
						run *new_run=compaction_reclaim_run(cm, target_run, target_version);
						version_invalidate_number_init(LSM.last_run_version, target_version);
						if(new_run->now_sst_num==0){
							version_clear_target(LSM.last_run_version, target_version, LSM.param.LEVELN-1);
							level_empty_run(LSM.disk[LSM.param.LEVELN-1], target_ridx);
						}
						else{
							level_update_run_at_move_originality(LSM.disk[LSM.param.LEVELN-1], target_ridx, new_run, false);
						}
						run_free(new_run);
					}
					else{
						req->start_level=req->end_level;
						req->end_level++;
						force=true;
						goto again;
					}
				}
				/*
				static int cnt=0;
				printf("force compaction! %u\n", cnt++);
				if(cnt==33){
					printf("break!\n");
				}
				last_level_reclaim(cm, LSM.param.LEVELN-1);
				goto end;*/
			}
		}

		force=false;
		tag_manager_free_tag(cm->tm,req->tag);
		req->end_req(req);
	}
	return NULL;
}

uint32_t compaction_read_param_remain_num(compaction_master *cm){
	return cm->read_param_queue->size();
}

inter_read_alreq_param *compaction_get_read_param(compaction_master *cm){
	if(cm->read_param_queue->size()==0){
		EPRINT("plz check size before get param", true);
	}
	inter_read_alreq_param *res=cm->read_param_queue->front();
	cm->read_param_queue->pop();
	return res;
}

void compaction_free_read_param(compaction_master *cm, inter_read_alreq_param *target){
	if(cm->read_param_queue->size()>COMPACTION_TAGS){
		EPRINT("debug point", false);
	}
	fdriver_destroy(&target->done_lock);
	cm->read_param_queue->push(target);
	return;
}
void compaction_wait(compaction_master *cm){
	uint32_t tag=tag_manager_get_tag(cm->tm);
	tag_manager_free_tag(cm->tm, tag);
}
