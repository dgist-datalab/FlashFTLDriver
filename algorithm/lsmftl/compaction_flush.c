#include "compaction.h"
#include "lsmtree.h"
#include "segment_level_manager.h"
#include "io.h"
#include <math.h>
extern lsmtree LSM;
extern uint32_t debug_lba;
extern uint32_t debug_piece_ppa;
typedef std::map<uint32_t, uint32_t>::iterator map_iter;


#ifdef MIN_ENTRY_PER_SST
static inline bool sequential_flag_update(int value){
#ifdef MIN_ENTRY_OFF
	return LSM.sst_sequential_available_flag;
#endif
	LSM.now_pinned_sst_file_num+=value;
	if(LSM.now_pinned_sst_file_num + 
			CEILING_TARGET(LSM.flushed_kp_set->size()-LSM.processed_entry_num, KP_IN_PAGE) < LSM.param.max_sst_in_pinned_level){
		LSM.sst_sequential_available_flag=true;
	}
	else{
		LSM.sst_sequential_available_flag=false;
	}
	return LSM.sst_sequential_available_flag;
}

static inline bool sequential_flag_test(int test){
#ifdef MIN_ENTRY_OFF
	return LSM.sst_sequential_available_flag;
#endif
	uint32_t test_num=LSM.now_pinned_sst_file_num+test;
	if(test_num + 
			CEILING_TARGET(LSM.flushed_kp_set->size()-LSM.processed_entry_num, KP_IN_PAGE) < LSM.param.max_sst_in_pinned_level){
		return true;
	}
	else{
		return false;
	}
}

static inline void flush_and_move(std::map<uint32_t, uint32_t> *kp_set, 
		std::map<uint32_t, uint32_t> * hot_kp_set,
		write_buffer *wb, uint32_t flushing_target_num){
	key_ptr_pair *temp_kp_set=write_buffer_flush(wb, flushing_target_num, false);
	std::pair<map_iter, bool> res_iter;
	for(uint32_t i=0; i<KP_IN_PAGE && temp_kp_set[i].lba!=UINT32_MAX; i++){
		LSM.flushed_kp_seg->insert(temp_kp_set[i].piece_ppa/L2PGAP/_PPS);
		std::map<uint32_t, uint32_t>::iterator find_iter, temp_iter;
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
			invalidate_kp_entry(find_iter->first, find_iter->second, UINT32_MAX, true);
			kp_set->erase(find_iter);

			if(hot_kp_set){
				if(LSM.unaligned_sst_file_set && LSM.unaligned_sst_file_set->now_sst_num){
					uint32_t idx=run_retrieve_sst_idx(LSM.unaligned_sst_file_set, temp_kp_set[i].lba);
					if(idx!=UINT32_MAX){
						invalidate_sst_file_map(&LSM.unaligned_sst_file_set->sst_set[idx]);
						run_remove_sst_file_at(LSM.unaligned_sst_file_set, idx);
					}
				}
				if(temp_kp_set[i].lba==debug_lba){
					printf("%u target moved to hot\n", debug_lba);
				}
				hot_kp_set->insert(
						std::pair<uint32_t, uint32_t>(temp_kp_set[i].lba, temp_kp_set[i].piece_ppa));
				continue;
			}
		}

		if(LSM.unaligned_sst_file_set && LSM.unaligned_sst_file_set->now_sst_num){
			uint32_t idx=run_retrieve_sst_idx(LSM.unaligned_sst_file_set, temp_kp_set[i].lba);
			if(idx!=UINT32_MAX){
				invalidate_sst_file_map(&LSM.unaligned_sst_file_set->sst_set[idx]);
				run_remove_sst_file_at(LSM.unaligned_sst_file_set, idx);
				if(LSM.unaligned_sst_file_set->now_sst_num==0){
					run_free(LSM.unaligned_sst_file_set);
					LSM.unaligned_sst_file_set=NULL;
				}
			}
		}

		if(LSM.same_segment_flag==UINT32_MAX){
			LSM.same_target_segment=SEGNUM(temp_kp_set[i].piece_ppa);
			LSM.same_segment_flag=1;
		}
		else if(LSM.same_segment_flag==1 && LSM.same_target_segment!=SEGNUM(temp_kp_set[i].piece_ppa)){
			LSM.same_segment_flag=0;
		}
		res_iter=kp_set->insert(
				std::pair<uint32_t, uint32_t>(temp_kp_set[i].lba, temp_kp_set[i].piece_ppa));

		find_iter=res_iter.first;
		if(LSM.sst_sequential_available_flag && find_iter!=kp_set->begin()){
			find_iter--;
			if(SEGNUM(find_iter->second)!=SEGNUM(temp_kp_set[i].piece_ppa) || 
					find_iter->second > temp_kp_set[i].piece_ppa){
				LSM.randomness_check++;
				if(LSM.randomness_check > LSM.param.max_sst_in_pinned_level){
					LSM.sst_sequential_available_flag=false;
				}
			}
		}
		version_coupling_lba_version(LSM.last_run_version, temp_kp_set[i].lba, UINT8_MAX);
	}
	free(temp_kp_set);
}

extern char all_set_page[PAGESIZE];
static sst_file *kp_to_sstfile(std::map<uint32_t, uint32_t> *flushed_kp_set,
		uint32_t* processed_entry_num,
		std::map<uint32_t, uint32_t>::iterator *temp_iter, 
		bool make_rh, bool make_random_sst, uint32_t limit_lba, bool *resolve_conflict){

#ifdef LSM_DEBUG
	static uint32_t cnt=0;
	//printf("kp_to_sstfile cnt:%u\n", ++cnt);
	if(cnt==315){
		//printf("break!\n");
	}
#endif

	if(temp_iter && *temp_iter==flushed_kp_set->end()){
		return NULL;
	}

	value_set *vs=inf_get_valueset(all_set_page, FS_MALLOC_W, PAGESIZE);
	key_ptr_pair *kp_set=(key_ptr_pair*)vs->value;

	std::map<uint32_t, uint32_t>::iterator iter=temp_iter?*temp_iter:flushed_kp_set->begin();
	uint32_t max_iter_cnt=KP_IN_PAGE;
	uint32_t i=0;
	read_helper *rh=NULL;

	uint32_t prev_seg_num=UINT32_MAX;
	uint32_t prev_piece_ppa=UINT32_MAX;
	bool now_sequential_file=!make_random_sst;

	bool min_seq_flag=false;
	uint32_t new_seq_cnt=0;
	uint32_t seq_start_idx=0;
	std::map<uint32_t, uint32_t>::iterator seq_start_iter;

	static bool function_debug_flag=false;

	uint32_t seg_idx=0;
	if((seg_idx=SEGNUM(page_manager_pick_new_ppa(LSM.pm, false, DATASEG)*L2PGAP)) != SEGNUM(iter->second)){
		/*this is an inevitable situation*/
		now_sequential_file=false;
	}

#ifdef MIN_ENTRY_OFF
	uint32_t check_prev_piece_ppa=UINT32_MAX;
	now_sequential_file=true;
#endif

	uint32_t prev_processed_entry_num=*processed_entry_num;

	for(; iter!=flushed_kp_set->end() && i<max_iter_cnt; i++, iter++ ){
		if(limit_lba!=UINT32_MAX && iter->first >=limit_lba){
			kp_set[i].lba=UINT32_MAX;
			kp_set[i].piece_ppa=UINT32_MAX;
			if(LSM.sst_sequential_available_flag==false){
				*resolve_conflict=true;	
				limit_lba=UINT32_MAX;
			//	continue;
			}
			else{
				*resolve_conflict=false;
				break;
			}
		}

		kp_set[i].lba=iter->first;
		kp_set[i].piece_ppa=iter->second;
#ifdef MIN_ENTRY_OFF
		if(seg_idx!=SEGNUM(kp_set[i].piece_ppa)){
			now_sequential_file=false;
		}
		else{
			if(check_prev_piece_ppa==UINT32_MAX){
				check_prev_piece_ppa=kp_set[i].piece_ppa;
			}
			else if(check_prev_piece_ppa > kp_set[i].piece_ppa){
				now_sequential_file=false;
			}
		}
		check_prev_piece_ppa=kp_set[i].piece_ppa;
#endif

//		if(LSM.global_debug_flag && kp_set[i].lba==debug_lba){
//			printf("%u target is into sstfile\n", debug_lba);
//		}
		slm_coupling_mem_lev_seg(SEGNUM(kp_set[i].piece_ppa), SEGPIECEOFFSET(kp_set[i].piece_ppa));

		if(now_sequential_file){
			if(prev_piece_ppa==UINT32_MAX){
				prev_piece_ppa=kp_set[i].piece_ppa;
				prev_seg_num=SEGNUM(kp_set[i].piece_ppa);
			}
			else{
				/*
				if(prev_seg_num!=SEGNUM(kp_set[i].piece_ppa)){
					printf("prev:%u now:%u\n", prev_seg_num, SEGNUM(kp_set[i].piece_ppa));
					EPRINT("should be align if the sstfile seq", true);
				}*/
				/*find unorderness*/
				if(!((prev_piece_ppa < kp_set[i].piece_ppa && kp_set[i].piece_ppa-prev_piece_ppa <=1) && prev_seg_num==SEGNUM(kp_set[i].piece_ppa))){
					/*checking available room for sstfile*/
					if(LSM.now_pinned_sst_file_num+ //prev made sstfile num
							CEILING_TARGET(flushed_kp_set->size()-(prev_processed_entry_num+i-1), KP_IN_PAGE)+//remaining sst file num 
							1/*now sstfile */ > LSM.param.max_sst_in_pinned_level){
						LSM.sst_sequential_available_flag=false;
						now_sequential_file=false;
						if(rh){
							read_helper_stream_insert(rh, kp_set[i].lba, kp_set[i].piece_ppa);
						}
						continue;
					}

					if(i<MIN_SEQ_ENTRY_NUM){
						min_seq_flag=true;
						now_sequential_file=false;
						if(rh){
							read_helper_stream_insert(rh, kp_set[i].lba, kp_set[i].piece_ppa);
						}
						prev_piece_ppa=kp_set[i].piece_ppa;
						continue;
					}

					kp_set[i].lba=UINT32_MAX;
					kp_set[i].piece_ppa=UINT32_MAX;
					break;
				}
				prev_piece_ppa=kp_set[i].piece_ppa;
			}
		}
		if(rh){
			read_helper_stream_insert(rh, kp_set[i].lba, kp_set[i].piece_ppa);
		}

		if(LSM.sst_sequential_available_flag && min_seq_flag){
			if(kp_set[i].piece_ppa-prev_piece_ppa-prev_piece_ppa==1){
				if(new_seq_cnt==0){
					seq_start_idx=i-1;
					seq_start_iter=std::prev(iter);
				}
				new_seq_cnt++;
				if(new_seq_cnt==4){
					iter=seq_start_iter;
					i=seq_start_idx;
					break;
				}
			}
			else{
				new_seq_cnt=0;
			}
			prev_piece_ppa=kp_set[i].piece_ppa;
		}
	}
	
	if(make_rh){
#ifdef DYNAMIC_HELPER_ASSIGN
		float density=(float)(i-1)/(kp_set[i-1].lba-kp_set[0].lba);
		if(density > LSM.param.BF_PLR_border && now_sequential_file){
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
			if(kp_set[j].lba==debug_lba){
				printf("break!\n");
			}
			read_helper_stream_insert(rh, kp_set[j].lba, kp_set[j].piece_ppa);
		}	
		read_helper_insert_done(rh);
	}

	*processed_entry_num+=i;
	*temp_iter=iter;

	if(i!=KP_IN_PAGE){
		kp_set[i].lba=UINT32_MAX;
		kp_set[i].piece_ppa=UINT32_MAX;
	}

	uint32_t last_idx=i-1;
	uint32_t map_ppa=page_manager_get_new_ppa(LSM.pm, false, DATASEG); //DATASEG for sequential tiering 
	validate_map_ppa(LSM.pm->bm, map_ppa, kp_set[0].lba,  kp_set[last_idx].lba, true);

	if(now_sequential_file && prev_seg_num!=UINT32_MAX &&
			SEGNUM(map_ppa*L2PGAP)!=prev_seg_num){
		EPRINT("sequential file should be aligned in block", true);
	}

	sst_file *res=sst_init_empty(now_sequential_file? BLOCK_FILE:PAGE_FILE);
	if(res->type==BLOCK_FILE){
		res->file_addr.piece_ppa=kp_set[0].piece_ppa;
		res->map_num=1;
		res->block_file_map=(map_range*)calloc(1,sizeof(map_range));
		res->block_file_map[0].start_lba=kp_set[0].lba;
		res->block_file_map[0].end_lba=kp_set[last_idx].lba;
		res->block_file_map[0].ppa=map_ppa;
		res->seq_data_end_piece_ppa=kp_set[last_idx].piece_ppa;
		res->end_ppa=map_ppa;
	}	
	else{
		res->file_addr.map_ppa=map_ppa;
		res->end_ppa=UINT32_MAX;
	}


	res->start_lba=kp_set[0].lba;
	res->end_lba=kp_set[last_idx].lba;
	res->_read_helper=rh;
	res->start_piece_ppa=kp_set[0].piece_ppa;

	res->sequential_file=now_sequential_file;

	if(LSM.global_debug_flag && res->start_lba <=debug_lba && res->end_lba>=debug_lba){
		printf("%u in start_lba:%u ~ end_lba:%u\n", debug_lba, res->start_lba, res->end_lba);
	}

	//printf("sstfile:%u~%u\n", res->start_lba, res->end_lba);

	algo_req *write_req=(algo_req*)malloc(sizeof(algo_req));
	write_req->type=MAPPINGW;
	write_req->param=(void*)vs;
	write_req->end_req=comp_alreq_end_req;
	io_manager_issue_internal_write(map_ppa, vs, write_req, false);

	if(function_debug_flag){
		printf("debug print\n");
		sst_print(res);
		function_debug_flag=false;
	}
	return res;
}


static inline uint32_t now_buffered_entry_num(){
		return LSM.flushed_kp_set->size()+LSM.flushed_kp_temp_set->size()+
#ifdef WB_SEPARATE
				LSM.hot_kp_set->size();
#else
				0;
#endif
}




static inline level *fa_make_pinned_level(level *pinned_level, 
		run **r, std::map<uint32_t, uint32_t> *kp_set, 
		uint32_t limit_sst_file_num){

	if(r && LSM.unaligned_sst_file_set){
		*r=NULL;
		return NULL;
	}
#ifdef LSM_DEBUG
	static int cnt=0;
	++cnt;
	printf("make pinned level cnt:%u %p \n",++cnt, r);
	if(cnt>=1644){
		//LSM.global_debug_flag=true;
		sst_file *temp_sptr;
		uint32_t temp_idx;
		map_iter temp_iter;
		if(!r && LSM.unaligned_sst_file_set){
			for_each_sst(LSM.unaligned_sst_file_set, temp_sptr, temp_idx){
				temp_iter=LSM.flushed_kp_set->find(temp_sptr->start_lba);	
				uint32_t entry_num=0;
				for(; temp_iter->first<=temp_sptr->end_lba && temp_iter!=LSM.flushed_kp_set->end(); temp_iter++){
					entry_num++;
				}

				if(entry_num!=read_helper_get_cnt(temp_sptr->_read_helper)){
					EPRINT("????", true);
				}
			}
		}
	}
#endif
	level *res=NULL;
	run *res_r=NULL;
	if(pinned_level){
		res=pinned_level;
	}
	else if(!r){
		res=level_init(LSM.param.max_sst_in_pinned_level, 1, LEVELING_WISCKEY, UINT32_MAX,
				kp_set->size(), false);
	}
	else if(r){
		res_r=run_init(LSM.param.max_sst_in_pinned_level, UINT32_MAX, 0);
	}

	uint32_t sst_num=LSM.unaligned_sst_file_set?LSM.unaligned_sst_file_set->now_sst_num:0;
	uint32_t sst_idx=0;
	sst_file *sptr=NULL;
	bool UST_flag=false;
	if(sst_num){
		UST_flag=true;
		if(sequential_flag_update(sst_num)==false){
			EPRINT("too many unaligned sstd", true);
		}
		uint32_t sidx;
		for_each_sst(LSM.unaligned_sst_file_set, sptr, sidx){
			LSM.processed_entry_num+=read_helper_get_cnt(sptr->_read_helper);
		}
		sptr=&LSM.unaligned_sst_file_set->sst_set[sst_idx];
	}

	map_iter m_iter=kp_set->begin();
	sst_file *res_file;
	uint32_t created_sst_file_num=0;
	while(1){
		res_file=NULL;
		uint32_t limit_lba=UINT32_MAX;
		if(UST_flag){
			limit_lba=sptr->start_lba;
		}

		if(m_iter->first==limit_lba){
		
		}
		else if(m_iter->first > limit_lba){
			EPRINT("logic error", true);
		}
		else{
			while(m_iter->first < limit_lba){
				static int cnt=0;
//				if(LSM.global_debug_flag){
//					printf("round :%u\n",cnt++);
//					if(cnt>=88){
//						printf("break!\n");
//					}
//					//LSM.global_debug_flag=false;
//				}
				bool resolve_conflict=false;
				uint32_t prev_processed_num=LSM.processed_entry_num;
				res_file=kp_to_sstfile(kp_set, &LSM.processed_entry_num, &m_iter, true, !LSM.sst_sequential_available_flag, limit_lba, &resolve_conflict);

				if(!res_file) break;
				if(r){
					LSM.processed_entry_num=prev_processed_num;
					run_append_sstfile_move_originality(res_r, res_file);
				}
				else{
					res_file->sequential_file?LSM.monitor.flushing_sequential_file++:LSM.monitor.flushing_random_file++;
					level_append_sstfile(res, res_file, true);
					sequential_flag_update(1);
				}
				/*check again*/
				if(resolve_conflict && sptr){
					if(sequential_flag_test(0)){
						LSM.sst_sequential_available_flag=true;
						goto out;
					}

					while(sptr->start_lba < res_file->end_lba && limit_lba!=UINT32_MAX){
						invalidate_sst_file_map(sptr);
						LSM.processed_entry_num-=read_helper_get_cnt(sptr->_read_helper);
						sequential_flag_update(-1);
						sst_idx++;
						if(sst_idx >= sst_num){
							UST_flag=false;
							limit_lba=UINT32_MAX;
						}
						else{
							sptr=&LSM.unaligned_sst_file_set->sst_set[sst_idx];
							limit_lba=sptr->start_lba;
						}
					}
				}
out:
				created_sst_file_num++;
				sst_free(res_file, LSM.pm);

//				if(LSM.global_debug_flag){
//					printf("now:%u remain:%lu - page:%lu\n", LSM.now_pinned_sst_file_num, 
//							LSM.flushed_kp_set->size()-LSM.processed_entry_num, 
//							CEILING_TARGET(LSM.flushed_kp_set->size()-LSM.processed_entry_num, KP_IN_PAGE));
//				}
				if(limit_sst_file_num == created_sst_file_num){
					break;
				}

				if(UST_flag && r && m_iter->first<limit_lba){
					EPRINT("can it be?", true);
				}
			}
			if(!res_file) break;
			if(limit_sst_file_num == created_sst_file_num){
				break;
			}
		}

		if(UST_flag){
			if(r){
				run_append_sstfile_move_originality(res_r, sptr);
			}
			else{
				sptr->sequential_file?LSM.monitor.flushing_sequential_file++:LSM.monitor.flushing_random_file++;
				level_append_sstfile(res, sptr, true);
			}
			m_iter=kp_set->upper_bound(sptr->end_lba);
			sst_idx++;
			if(sst_idx >= sst_num){
				UST_flag=false;
				limit_lba=UINT32_MAX;
			}
			else{
				sptr=&LSM.unaligned_sst_file_set->sst_set[sst_idx];
				limit_lba=sptr->start_lba;
			}
		}

		if(m_iter==kp_set->end()){
			break;
		}
	}
	if(r){
		*r=res_r;
	}
	return res;
}

level *make_pinned_level(std::map<uint32_t, uint32_t> * kp_set){
	lsmtree_init_ordering_param();
	LSM.pinned_level=fa_make_pinned_level(LSM.pinned_level, NULL, kp_set, UINT32_MAX);
	level *res=LSM.pinned_level;
	lsmtree_init_ordering_param();
	return res;
}

level* flush_memtable(write_buffer *wb, bool is_gc_data){
	//static int cnt=0;
	//printf("flush cnt:%u\n", cnt++);
	if(page_manager_get_total_remain_page(LSM.pm, false, false) <= MAX(wb->buffered_entry_num/L2PGAP, 2)){
		__do_gc(LSM.pm, false, KP_IN_PAGE/L2PGAP);
	}
	rwlock_write_lock(&LSM.flush_wait_wb_lock);
	rwlock_write_lock(&LSM.flushed_kp_set_lock);
	if(LSM.flushed_kp_set==NULL){
		LSM.flushed_kp_set=new std::map<uint32_t, uint32_t>();
		LSM.flushed_kp_temp_set=new std::map<uint32_t, uint32_t>();
		lsmtree_init_ordering_param();
#ifdef WB_SEPARATE
		LSM.hot_kp_set=new std::map<uint32_t, uint32_t>();
#endif
	}
	level *res=NULL;

	bool making_level=false;
	uint32_t now_remain_page_num;

	now_remain_page_num=page_manager_get_remain_page(LSM.pm, false);
	if(now_remain_page_num<2){
		now_remain_page_num=page_aligning_data_segment(LSM.pm, 2);
	}
/*
	if(now_remain_page_num < wb->data->size()/L2PGAP+1){
		printf("alinging called!\n");
		map_iter m_iter;
		m_iter=LSM.flushed_kp_set->begin();
		run *temp_run;

		uint32_t target_limit_file_num=LSM.param.max_sst_in_pinned_level-
			CEILING_TARGET(LSM.param.write_buffer_ent-now_buffered_entry_num(),KP_IN_PAGE);

		fa_make_pinned_level(NULL, &temp_run, LSM.flushed_kp_set, MIN(now_remain_page_num,target_limit_file_num));
		if(temp_run){
			if(LSM.unaligned_sst_file_set){
				run_free(LSM.unaligned_sst_file_set);
			}

			LSM.unaligned_sst_file_set=temp_run;
		}

		now_remain_page_num=page_manager_get_remain_page(LSM.pm, false);
		if(now_remain_page_num<2){
			now_remain_page_num=page_aligning_data_segment(LSM.pm, 2);
		}
	}
*/
	
	if(wb->data->size()){
		flush_and_move(LSM.flushed_kp_set,
#ifdef WB_SEPARATE
				LSM.hot_kp_set,
#else
				NULL,
#endif
				wb, UINT32_MAX);
	}

	now_remain_page_num=page_manager_get_remain_page(LSM.pm, false);
	uint32_t needed_map_num=CEILING_TARGET(LSM.flushed_kp_set->size(), KP_IN_PAGE);
	needed_map_num-=LSM.unaligned_sst_file_set?LSM.unaligned_sst_file_set->now_sst_num:0;
	int32_t after_map_write_remain_page=now_remain_page_num-needed_map_num;
//#ifndef MIN_ENTRY_OFF
	if(LSM.same_segment_flag && LSM.sst_sequential_available_flag && 
			(after_map_write_remain_page>=0 && after_map_write_remain_page <2)){
		run *temp_run;
		if(LSM.unaligned_sst_file_set){
			EPRINT("processing?", true);
		}
		fa_make_pinned_level(NULL, &temp_run, LSM.flushed_kp_set, needed_map_num);
		if(temp_run){
			if(LSM.unaligned_sst_file_set){
				run_free(LSM.unaligned_sst_file_set);
			}

			LSM.unaligned_sst_file_set=temp_run;
		}
	}
//#endif

	if(now_buffered_entry_num() >= LSM.param.write_buffer_ent){
		making_level=true;
	}

	/*make temp level*/
	if(making_level){
		if(needed_map_num > now_remain_page_num){
			__do_gc(LSM.pm, false, needed_map_num-now_remain_page_num);
		}
		LSM.pinned_level=fa_make_pinned_level(LSM.pinned_level, NULL,LSM.flushed_kp_set, UINT32_MAX);
		res=LSM.pinned_level;
	}
	else{
		res=NULL;
	}

	write_buffer_free(wb);
	LSM.flush_wait_wb=NULL;
	rwlock_write_unlock(&LSM.flushed_kp_set_lock);
	rwlock_write_unlock(&LSM.flush_wait_wb_lock);
	if(res){
		lsmtree_init_ordering_param();
#ifdef LSM_DEBUG
		sst_file *sptr;
		run *rptr;
		uint32_t sidx, ridx;
		uint32_t sequential_cnt=0;
		for_each_sst_level(res, rptr, ridx, sptr, sidx){
			if(sptr->sequential_file){
				sequential_cnt++;
			}
		}
		printf("sequntial file ratio:%u/%u\n",sequential_cnt, res->now_sst_num);
#endif
	}
	return res;
}
#endif
