#include "./gc.h"
#include "./piece_ppa.h"
#include "../../include/data_struct/list.h"
#include "./shortcut.h"
#include "./page_aligner.h"
#include "./lsmtree.h"
extern uint32_t test_piece_ppa;
extern sc_master *shortcut;
extern lower_info *g_li;
extern bool debug_flag;
extern lsmtree *LSM;
extern uint32_t test_key;
static void *__gc_end_req(algo_req *req){
	gc_value *g_value=(gc_value*)req->param;
	switch(req->type){
		case GCDR:
		case GCMR:
			fdriver_unlock(&g_value->lock);
			break;
		case GCDW:
			inf_free_valueset(g_value->value, FS_MALLOC_R);
			free(g_value);
			break;
		case GCMW:
			inf_free_valueset(g_value->value, FS_MALLOC_W);
			free(g_value);
			break;
	}
	free(req);
	return NULL;
}

static algo_req *__get_algo_req(gc_value *g_value, uint32_t type){
	algo_req *res=(algo_req*)malloc(sizeof(algo_req));
	res->type=type;
	res->ppa=g_value->ppa;
	res->value=g_value->value;
	res->end_req=__gc_end_req;
	res->param=(void*)g_value;
	res->parents=NULL;
	return res;
}

static gc_value* __gc_issue_read(uint32_t ppa, blockmanager *sm, uint32_t type){
	gc_value *g_value=(gc_value*)malloc(sizeof(gc_value));
	g_value->ppa=ppa;
	g_value->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	fdriver_mutex_init(&g_value->lock);
	fdriver_lock(&g_value->lock);
	algo_req *res=__get_algo_req(g_value, type);
	memcpy(g_value->oob, sm->get_oob(sm, res->ppa), sizeof(uint32_t) * L2PGAP);
	g_li->read(res->ppa, PAGESIZE, res->value, res);
	return g_value;
}


static inline void __gc_issue_write(gc_value *g_value, blockmanager *sm, uint32_t type){
	algo_req *res=__get_algo_req(g_value, type);
	sm->set_oob(sm, (char*)g_value->oob, sizeof(uint32_t) *L2PGAP, res->ppa);
	g_li->write(res->ppa, PAGESIZE, res->value, res);
}

static void *__temp_gc_end_req(algo_req *req){
	value_set *value=(value_set*)req->param;
	inf_free_valueset(value, FS_MALLOC_W);
	free(req);
	return NULL;
}

static inline void __gc_issue_temp_meta_write(uint32_t ppa, value_set *value, 
blockmanager *sm, uint32_t start_lba, uint32_t end_lba){
	uint32_t *oob=(uint32_t*)sm->get_oob(sm, ppa);
	oob[0]=start_lba;
	oob[1]=end_lba;
	algo_req *res=(algo_req *)malloc(sizeof(algo_req));
	res->type=GCMW_DGC;
	res->ppa=ppa;
	res->value=value;
	res->end_req=__temp_gc_end_req;
	res->param=(void*)value;
	g_li->write(ppa, PAGESIZE, value, res);
}

static inline void __gc_read_check(gc_value* g_value){
	fdriver_lock(&g_value->lock);
	fdriver_destroy(&g_value->lock);
}

static inline void __clear_block_and_gsegment(L2P_bm *bm, __gsegment *target){
	__block *blk;
	uint32_t bidx;
	for_each_block(target, blk, bidx){
		if(blk->block_idx==test_piece_ppa/L2PGAP/_PPB){
			static int cnt=0;
			printf("%u clear[%u]\n", blk->block_idx, cnt++);
		}
		L2PBm_clear_block_info(bm, blk->block_idx);
	}
	blockmanager *sm=bm->segment_manager;
	sm->trim_segment(sm, target);
}

enum{
	GET_RPPA, GET_PPA, GET_MIXED,
};

uint32_t gc_summary_get_rppa(L2P_bm *bm, uint32_t flag){
	uint32_t rppa;
	switch (flag)
	{
	case GET_RPPA:
		rppa = L2PBm_get_map_rppa(bm);
		break;
	case GET_PPA:
		rppa = L2PBm_get_map_ppa(bm);
		break;
	case GET_MIXED:
		rppa = L2PBm_get_map_ppa_mixed(bm);
		break;
	}
	return rppa;
}

void gc_summary_segment(L2P_bm *bm, __gsegment *target, uint32_t activie_assign){
	blockmanager *sm=bm->segment_manager;
	list *temp_list=list_init();
	uint32_t page, bidx, pidx;
	gc_value *g_value;
	for_each_page_in_seg(target,page,bidx,pidx){
		bool read_flag=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(sm->is_invalid_piece(sm, page * L2PGAP+i)) continue;
			else{
				read_flag=true;
				break;
			}
		}
		if(read_flag){
			g_value=__gc_issue_read(page, sm, GCMR);
			list_insert(temp_list, (void*)g_value);
		}
	}

	li_node *now,*nxt;
	gc_value *write_gc_value=NULL;
	uint32_t gc_idx=0;
	uint32_t rppa;

	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){
			g_value=(gc_value*)now->data;
			__gc_read_check(g_value);

			for(uint32_t i=0; i<L2PGAP; i++){
				if(sm->is_invalid_piece(sm, g_value->ppa*L2PGAP+i)) continue;
				uint32_t start_lba=g_value->oob[i];
				uint32_t intra_idx;
				uint32_t target_piece_ppa=g_value->ppa*L2PGAP+i;
				sid_info* info=sorted_array_master_get_info_mapgc(start_lba, target_piece_ppa, &intra_idx);
				if(info==NULL || info->sa->sp_meta[intra_idx].piece_ppa!=target_piece_ppa){
					EPRINT("mapping error", true);
				}

				if(invalidate_piece_ppa(sm, target_piece_ppa, true) ==BIT_ERROR){
					EPRINT("invalidate map error", true);
				}

				if(write_gc_value==NULL){
					write_gc_value=(gc_value*)calloc(1, sizeof(gc_value));
					write_gc_value->value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
					rppa=gc_summary_get_rppa(bm, activie_assign);
					write_gc_value->ppa=rppa;
				}
				uint32_t target_r_piece_ppa=rppa*L2PGAP+gc_idx;
				if(validate_piece_ppa(sm, target_r_piece_ppa, true)==BIT_ERROR){
					EPRINT("validate map error", true);
				}
				info->sa->sp_meta[intra_idx].piece_ppa=target_r_piece_ppa;
				write_gc_value->oob[gc_idx]=g_value->oob[i];
				memcpy(&write_gc_value->value->value[(gc_idx)*LPAGESIZE], &g_value->value->value[(i)*LPAGESIZE], LPAGESIZE);
				gc_idx++;

				if(gc_idx==L2PGAP){
					__gc_issue_write(write_gc_value, sm,GCMW);
					write_gc_value=NULL;
					gc_idx=0;
				}

			}
			inf_free_valueset(g_value->value, FS_MALLOC_R);
			free(g_value);
			list_delete_node(temp_list,now);
		}
	}

	if(write_gc_value){
		__gc_issue_write(write_gc_value, sm, GCMW);
	}

	__clear_block_and_gsegment(bm, target);
	if(activie_assign==GET_RPPA){
		bm->now_summary_seg = bm->reserve_summary_seg;
		sm->change_reserve_to_active(sm, bm->reserve_summary_seg);
		bm->reserve_summary_seg = sm->get_segment(sm, BLOCK_RESERVE);
		L2PBm_set_seg_type(bm, bm->reserve_summary_seg->seg_idx, SUMMARY_SEG);
	}
	list_free(temp_list);
}

typedef struct gc_block{
	block_info *b_info;
	__block *blk;
}gc_block;

extern uint32_t target_PBA;
static inline void copy_normal_block(L2P_bm *bm, list *blk_list){
	if(blk_list->size==0) return;
	li_node *now, *nxt, *p_now, *p_nxt;
	blockmanager *sm=bm->segment_manager;
	list *temp_list=list_init();
	gc_value *g_value;
	for_each_list_node_safe(blk_list, now, nxt){
	//	DEBUG_CNT_PRINT(gc_cnt, 29, __FUNCTION__, __LINE__);
		gc_block *g_blk=(gc_block*)now->data;
		uint32_t bidx=g_blk->blk->block_idx;
		sid_info* info=sorted_array_master_get_info(g_blk->b_info->sid);
		if(info->sa==NULL){
			EPRINT("not found info at sid:%u", false, g_blk->b_info->sid);
			GDB_MAKE_BREAKPOINT; //not be commented
			info=sorted_array_master_get_info(g_blk->b_info->sid);
		}
		uint32_t invalidate_piece_num=0, validate_piece_num=0;

		/*read all page*/
		for(uint32_t i=0; i<_PPB; i++){
			uint32_t page=bidx*_PPB+i;
			bool read_flag=false;
			for(uint32_t j=0; j<L2PGAP; j++){
				if(sm->is_invalid_piece(sm, page*L2PGAP+j)){
					invalidate_piece_num++;
					continue;
				}
				validate_piece_num++;
				read_flag=true;
				break;
			}

			if(read_flag){
				g_value=__gc_issue_read(page, sm, GCDR);
				list_insert(temp_list, (void*)g_value);
			}
		}

		uint32_t new_pba=L2PBm_pick_empty_RPBA(bm);
		uint32_t prev_ppa=UINT32_MAX;
		bool valid_data_ptr[L2PGAP];
		uint32_t idx=g_blk->b_info->intra_idx;
		L2PBm_make_map(bm, new_pba, info->sid, idx);

		/*write all page to new address*/
		while(temp_list->size){
			for_each_list_node_safe(temp_list, p_now, p_nxt){
				g_value=(gc_value*)p_now->data;
				__gc_read_check(g_value);

				for(uint32_t j=0; j<L2PGAP; j++){
					if(sm->is_invalid_piece(sm, g_value->ppa*L2PGAP+j)){
						valid_data_ptr[j]=false;
						continue;
					}
					valid_data_ptr[j]=true;
					if(invalidate_piece_ppa(sm, g_value->ppa*L2PGAP+j, true)==BIT_ERROR){
						EPRINT("bit error in normal block copy", true);
					}
				}


				uint32_t start_rppa=new_pba+(g_value->ppa%_PPB);
				if(prev_ppa==UINT32_MAX || prev_ppa<start_rppa){
					prev_ppa=start_rppa;
				}
				else{
					EPRINT("start_rppa should be increase", true);
				}
				g_value->ppa=start_rppa;

				sm->set_oob(sm, (char*)g_value->oob, sizeof(uint32_t)*L2PGAP, g_value->ppa);
				for(uint32_t j=0; j<L2PGAP; j++){
					if(!valid_data_ptr[j]){
						continue;
					}
					if(validate_piece_ppa(sm, g_value->ppa*L2PGAP+j, true)==BIT_ERROR){
						EPRINT("bit error in normal block copy", true);
					}
				}

				__gc_issue_write(g_value, sm, GCDW);
				list_delete_node(temp_list, p_now);
			}
		}

		/*update block mapping*/
		if(info->sa->pba_array[idx].PBA!=bidx*_PPB){
			EPRINT("inaccurate block! target:%u, sid_pba:%u", true, bidx*_PPB, info->sa->pba_array[idx].PBA);
		}
		info->sa->pba_array[idx].PBA=new_pba;

		free(g_blk);
		list_delete_node(blk_list, now);
	}

	list_free(temp_list);
}

static inline void __gc_issue_write_temp(value_set *value, uint32_t ppa,
		char *oob, blockmanager *sm){
	gc_value *temp_value=(gc_value*)malloc(sizeof(gc_value));
	temp_value->value=value;
	temp_value->ppa=ppa;
	memcpy(temp_value->oob, oob, sizeof(uint32_t) *L2PGAP);
	__gc_issue_write(temp_value, sm, GCDW);
}

typedef struct pinned_info{
	run *r;
	uint32_t intra_offset;
	uint32_t old_psa;
	uint32_t new_piece_ppa;
	uint32_t ste_num;
	uint32_t lba;
	bool update_target;
}pinned_info;

static inline void __update_pinning(uint32_t *lba_set, pinned_info *pset, uint32_t ppa,
		uint32_t num, blockmanager *bm){
	for(uint32_t i=0; i<num; i++){
		run *r=pset[i].r;
		if(validate_piece_ppa(bm, ppa*L2PGAP+i, true)==BIT_ERROR){
			EPRINT("validate piece ppa error in frag blk copy", true);
		}
		st_array_update_pinned_info(r->st_body, pset[i].ste_num, pset[i].intra_offset, ppa*L2PGAP+i, pset[i].old_psa);
	}
}

static inline uint32_t __processing_unlinked_psa(block_info *binfo, uint32_t lba, uint32_t psa, bool mixed){
	bool processed=false;
	uint32_t intra_offset;
	uint32_t ste_num;
	for(uint32_t k=0; k<MAX_RUN_NUM_FOR_FRAG; k++){
		if((binfo->frag_info & (1<<k))==0) continue;
		sid_info* temp_sid=sorted_array_master_get_info(k);
		if(temp_sid==NULL) continue;
		if(temp_sid->r==NULL) continue;
		intra_offset=run_find_include_address_byself(temp_sid->r, lba, psa, &ste_num);
		if(intra_offset==NOT_FOUND) continue;

		st_array_unlink_bit_set(temp_sid->sa, ste_num, intra_offset, psa);
		processed=true;
		break;
	}
	if(!processed){
		if(mixed){
			//the data reside in binfo, but new lba was written
			return UINT32_MAX;
		}
		else{
			EPRINT("unlinking psa failed", true);
		}
	}
	return UINT32_MAX;
}

static inline void copy_frag_block(L2P_bm *bm, list *frag_list){
	if(frag_list->size==0) return;
	//DEBUG_CNT_PRINT(cnt_gc_data, UINT32_MAX, __FUNCTION__, __LINE__);
	li_node *now, *nxt, *p_now, *p_nxt;
	blockmanager *sm=bm->segment_manager;
	list *temp_list=list_init();
	gc_value *g_value;
	pp_buffer *buffer=NULL;
	uint32_t rppa=UINT32_MAX;
	pinned_info pset[L2PGAP];
	for_each_list_node_safe(frag_list, now, nxt){
		gc_block *g_blk=(gc_block*)now->data;
		uint32_t bidx=g_blk->blk->block_idx;

		/*read all page*/
		for(uint32_t i=0; i<_PPB; i++){
			uint32_t page=bidx*_PPB+i;
			bool read_flag=false;
			for(uint32_t j=0; j<L2PGAP; j++){
				if(sm->is_invalid_piece(sm, page*L2PGAP+j)){
					continue;
				}
				read_flag=true;
				break;
			}

			if(read_flag){
				g_value=__gc_issue_read(page, sm, GCDR);
				list_insert(temp_list, (void*)g_value);
			}
		}

		while(temp_list->size){
			for_each_list_node_safe(temp_list, p_now, p_nxt){
				g_value=(gc_value*)p_now->data;
				if(buffer==NULL){
					buffer=pp_init();
				}
				__gc_read_check(g_value);

				for(uint32_t j=0; j<L2PGAP; j++){
					if(sm->is_invalid_piece(sm, g_value->ppa*L2PGAP+j)){
						continue;
					}

					uint32_t lba=g_value->oob[j];
					uint32_t psa=g_value->ppa*L2PGAP+j;
					uint32_t intra_offset;
					uint32_t ste_num;

					run *r=run_find_include_address(LSM->shortcut, lba, psa, &ste_num, &intra_offset);
					if(r==NULL){ //unlinked ppa;
						block_info *binfo=g_blk->b_info;
						__processing_unlinked_psa(binfo, lba, psa, false);
						continue;
					}
					else{
						pset[buffer->buffered_num].r=r;
						pset[buffer->buffered_num].intra_offset=intra_offset;
						pset[buffer->buffered_num].old_psa=psa;
						pset[buffer->buffered_num].ste_num=ste_num;
					}

					if(invalidate_piece_ppa(sm, psa,true)==BIT_ERROR){
						EPRINT("bit error in frag block copy", true);
					}

					if(pp_insert_value(buffer, lba, &g_value->value->value[LPAGESIZE*j])){
						if(rppa==UINT32_MAX){
							rppa=L2PBm_pick_empty_RPBA(bm);
						}
						value_set *value=pp_get_write_target(buffer, false);
						__gc_issue_write_temp(value,  rppa, (char*)buffer->LBA, sm);
						__update_pinning(buffer->LBA, pset, rppa, buffer->buffered_num, sm);
						
						pp_reinit_buffer(buffer);
						rppa++;
						if(rppa%_PPB==0){
							rppa=UINT32_MAX;
						}
					}
				}

				inf_free_valueset(g_value->value, FS_MALLOC_R);
				free(g_value);
				list_delete_node(temp_list, p_now);
			}
		}
		free(g_blk);
		list_delete_node(frag_list, now);
	}
	list_free(temp_list);

	if(buffer && buffer->buffered_num){
		if(rppa==UINT32_MAX){
			rppa=L2PBm_pick_empty_RPBA(bm);
		}
		value_set *value=pp_get_write_target(buffer, true);
		__gc_issue_write_temp(value, rppa, (char*)buffer->LBA, sm);
		__update_pinning(buffer->LBA, pset, rppa, buffer->buffered_num, sm);
	}
	if(buffer){
		pp_free(buffer);
	}
}

static inline bool __normal_block_valid_check(blockmanager *sm, uint32_t block_idx){
	uint32_t invalidate_cnt=0;
	uint32_t validate_cnt=0;
	bool res=0;
	for(uint32_t i=0; i<_PPB; i++){
		uint32_t page=block_idx*_PPB+i;
		for(uint32_t j=0; j<L2PGAP; j++){
			if(sm->is_invalid_piece(sm, page*L2PGAP+j)){
				invalidate_cnt++;
			}
			else{
				validate_cnt++;
				break;
			}
		}
	}
	return invalidate_cnt!=L2PGAP *_PPB;
}

static inline void copy_mixed_block(L2P_bm *bm, list *blk_list){
	if(blk_list->size==0) return;
	//DEBUG_CNT_PRINT(cnt_gc_data, UINT32_MAX, __FUNCTION__, __LINE__);
	li_node *now, *nxt, *p_now, *p_nxt;
	blockmanager *sm=bm->segment_manager;
	list *temp_list=list_init();
	gc_value *g_value;
	pinned_info pset[L2PGAP];
	for_each_list_node_safe(blk_list, now, nxt){
		gc_block *g_blk=(gc_block*)now->data;
		uint32_t bidx=g_blk->blk->block_idx;
		sid_info* info=sorted_array_master_get_info(g_blk->b_info->sid);
		if(info->sa==NULL){
			EPRINT("not found info at sid:%u", false, g_blk->b_info->sid);
			GDB_MAKE_BREAKPOINT; //not be commented
			info=sorted_array_master_get_info(g_blk->b_info->sid);
		}
		uint32_t invalidate_piece_num=0, validate_piece_num=0;

		/*read all page*/
		for(uint32_t i=0; i<_PPB; i++){
			uint32_t page=bidx*_PPB+i;
			bool read_flag=false;
			for(uint32_t j=0; j<L2PGAP; j++){
				if(sm->is_invalid_piece(sm, page*L2PGAP+j)){
					invalidate_piece_num++;
					continue;
				}
				validate_piece_num++;
				read_flag=true;
				break;
			}

			if(read_flag){
				g_value=__gc_issue_read(page, sm, GCDR);
				list_insert(temp_list, (void*)g_value);
			}
		}
		if(bidx==test_piece_ppa/L2PGAP/_PPB){
			printf("%u block including target gced copy_num:%u\n", test_piece_ppa, temp_list->size);
		}

		if(temp_list->size==0){
			info->sa->sp_meta[g_blk->b_info->intra_idx].all_reinsert=true;
			free(g_blk);
			list_delete_node(blk_list, now);
			 continue;
		}

		uint32_t new_pba=L2PBm_pick_empty_RPBA(bm);
		uint32_t prev_ppa=UINT32_MAX;
		bool valid_data_ptr[L2PGAP];
		uint32_t idx=g_blk->b_info->intra_idx;
		L2PBm_make_map_mixed(bm, new_pba, info->sid, idx, g_blk->b_info->frag_info);

		while(temp_list->size){
			for_each_list_node_safe(temp_list, p_now, p_nxt){
				g_value=(gc_value*)p_now->data;
				__gc_read_check(g_value);

				for(uint32_t j=0; j<L2PGAP; j++){
					pset[j].update_target=false;
					if(sm->is_invalid_piece(sm, g_value->ppa*L2PGAP+j)){
						valid_data_ptr[j]=false;
						continue;
					}

					valid_data_ptr[j]=true;
					pset[j].lba=g_value->oob[j];
					pset[j].old_psa=g_value->ppa*L2PGAP+j;

					uint32_t ste_num; 
					uint32_t intra_offset;
					run *r=run_find_include_address_for_mixed(LSM->shortcut, pset[j].lba, pset[j].old_psa, &ste_num, &intra_offset);
					if(intra_offset==NOT_FOUND){
						if(r && r->st_body->pba_array[ste_num].PBA==bidx*_PPB){
							pset[j].r=NULL;
							pset[j].update_target=false;
						}
						else{
							pset[j].r=NULL;
							pset[j].update_target=true;
						}
					}
					else{
						pset[j].r=r;
						pset[j].update_target=true;
						pset[j].ste_num=ste_num;
						pset[j].intra_offset=intra_offset;
					}

					if(invalidate_piece_ppa(sm, g_value->ppa*L2PGAP+j, true)==BIT_ERROR){
						EPRINT("bit error in normal block copy", true);
					}
				}
					
				uint32_t start_rppa=new_pba+(g_value->ppa%_PPB);
				if(prev_ppa==UINT32_MAX || prev_ppa<start_rppa){
					prev_ppa=start_rppa;
				}
				else{
					EPRINT("start_rppa should be increase", true);
				}
				g_value->ppa=start_rppa;

				sm->set_oob(sm, (char*)g_value->oob, sizeof(uint32_t)*L2PGAP, g_value->ppa);
				for(uint32_t j=0; j<L2PGAP; j++){
					if(!valid_data_ptr[j]){
						continue;
					}
					pset[j].new_piece_ppa=g_value->ppa*L2PGAP+j;
					if(pset[j].lba==test_key){
						printf("target %u moved to %u\n", pset[j].lba, pset[j].new_piece_ppa);
					}
					if(validate_piece_ppa(sm, g_value->ppa*L2PGAP+j, true)==BIT_ERROR){
						EPRINT("bit error in normal block copy", true);
					}
				}

				__gc_issue_write(g_value, sm, GCDW);
				
				for(uint32_t j=0; j<L2PGAP; j++){
					if(pset[j].update_target==false) continue;
					if(pset[j].r){
						st_array_update_pinned_info(pset[j].r->st_body, pset[j].ste_num, pset[j].intra_offset, pset[j].new_piece_ppa, pset[j].old_psa);
					}
					else{
						__processing_unlinked_psa(g_blk->b_info, pset[j].lba, pset[j].old_psa, true);
					}
				}


				list_delete_node(temp_list, p_now);
			}
		}

		/*update block mapping*/
		if(info->sa->pba_array[idx].PBA!=bidx*_PPB){
			EPRINT("inaccurate block! target:%u, sid_pba:%u", true, bidx*_PPB, info->sa->pba_array[idx].PBA);
		}
		info->sa->pba_array[idx].PBA=new_pba;

		free(g_blk);
		list_delete_node(blk_list, now);
	}
}

uint32_t gc_data_segment(L2P_bm *bm, __gsegment *target){
	static uint32_t cnt=0;
	DEBUG_CNT_PRINT(cnt_gc_data, UINT32_MAX, __FUNCTION__, __LINE__);
	printf("target seg:%u\n",target->seg_idx);
	if(cnt > 1200 && target->seg_idx==149){
	//	GDB_MAKE_BREAKPOINT;
	}
	blockmanager *sm=bm->segment_manager;
	list *normal_list=list_init();
	list *mixed_list=list_init();
	list *frag_list=list_init();
	__block *blk;
	uint32_t bidx;
	for_each_block(target, blk, bidx){
		gc_block *temp=(gc_block*)malloc(sizeof(gc_block));
		temp->b_info=&bm->PBA_map[blk->block_idx];
		temp->blk=blk;
		switch(bm->PBA_map[blk->block_idx].type){
			case LSM_BLOCK_NORMAL:
				if(__normal_block_valid_check(sm, blk->block_idx)){
					list_insert(normal_list, (void*)temp);
				}
				else{
					free(temp);
				}
				break;
			case LSM_BLOCK_MIXED:
				list_insert(mixed_list, (void*)temp);
				break;
			case LSM_BLOCK_FRAGMENT:
				list_insert(frag_list, (void*)temp);
				break;
			default:
				EPRINT("EMPTY BLOCK!", true);
				break;
		}
	}

	copy_normal_block(bm, normal_list);
	copy_mixed_block(bm, mixed_list);
	copy_frag_block(bm, frag_list);

	__clear_block_and_gsegment(bm, target);

	list_free(normal_list);
	list_free(frag_list);
	uint32_t res=BPS-bm->reserve_block_idx;
	bm->now_seg_idx=bm->reserve_seg->seg_idx;
	bm->now_block_idx=bm->reserve_block_idx;
	sm->change_reserve_to_active(sm, bm->reserve_seg);
	bm->reserve_block_idx=0;
	bm->reserve_seg=sm->get_segment(sm, BLOCK_RESERVE);
	L2PBm_set_seg_type(bm, bm->reserve_seg->seg_idx, DATA_SEG);
	return res;
}

void __compact_summary_block(L2P_bm * bm, __gsegment **target, uint32_t target_num){
	for(uint32_t i=0; i<target_num; i++){
		gc_summary_segment(bm, target[i], GET_MIXED);
	}
	blockmanager *sm=bm->segment_manager;
	if(sm->check_full(bm->now_summary_seg)){
		bm->now_summary_seg = bm->reserve_summary_seg;
		sm->change_reserve_to_active(sm, bm->reserve_summary_seg);
		bm->reserve_summary_seg = sm->get_segment(sm, BLOCK_RESERVE);
		L2PBm_set_seg_type(bm, bm->reserve_summary_seg->seg_idx, SUMMARY_SEG);	
	}
}

uint32_t gc(L2P_bm *bm, uint32_t type){
	blockmanager *sm=bm->segment_manager;
	__gsegment *target=sm->get_gc_target(sm);
	bool full_invalid=target->invalidate_piece_num==target->validate_piece_num;

	//printf("target type:%s\n", bm->seg_type[target->seg_idx]==SUMMARY_SEG?"SUMMARY":"DATA");

	if(full_invalid){
		__clear_block_and_gsegment(bm, target);
		return GC_TRIM;
	}

	std::queue<uint32_t> temp_seg_q;
	if(type==DATA_SEG && bm->seg_type[target->seg_idx]==SUMMARY_SEG){
		if(	bm->now_summary_seg->seg_idx!=target->seg_idx &&
			_PPS-bm->now_summary_seg->used_page_num >= (target->validate_piece_num-target->invalidate_piece_num)/L2PGAP){
			gc_summary_segment(bm, target, GET_PPA);
			return GC_DIFF_SEG;
		}else{
			printf("now remain piece_num:%u(%u), %u\n", _PPS-bm->now_summary_seg->used_page_num, bm->now_summary_seg->validate_piece_num-bm->now_summary_seg->invalidate_piece_num,
			target->validate_piece_num-target->invalidate_piece_num);
		}
	}

	while(bm->seg_type[target->seg_idx]!=type){
		temp_seg_q.push(target->seg_idx);
		free(target);
		target=sm->get_gc_target(sm);
		printf("target type:%s\n", bm->seg_type[target->seg_idx]==SUMMARY_SEG?"SUMMARY":"DATA");
		if(target==NULL){
			EPRINT("dev full", true);
		}
	}

	if(type==DATA_SEG){
		uint32_t all_invalid_block_num=0;
		for(uint32_t i=0; i<BPS; i++){
			if(target->blocks[i]->is_full_invalid){
				all_invalid_block_num++;
			}
		}
	}

	uint32_t target_idx=0;
	uint32_t free_block=0;
	switch(type){
		case SUMMARY_SEG:
			gc_summary_segment(bm, target, GET_RPPA);
			break;
		case DATA_SEG:
			target_idx=target->seg_idx;
			free_block=gc_data_segment(bm, target);
			break;
	}

	while(temp_seg_q.size()){
		uint32_t seg_idx=temp_seg_q.front();
		sm->insert_gc_target(sm, seg_idx);
		temp_seg_q.pop();
	}
	return GC_COPY;
}

bool gc_check_enough_space(L2P_bm *bm, uint32_t target_pba_num){
	if(L2PBm_get_free_block_num(bm) >= target_pba_num){
		return true;
	}

	blockmanager *sm=bm->segment_manager;
	std::queue<uint32_t> temp_seg_q;
	bool res;

	uint32_t free_block_num = 0;
	bool diff_gc=false;
	while (free_block_num < target_pba_num)
	{	
		__gsegment *target=sm->get_gc_target(sm);
		if(target==NULL){
			res=false;
			goto out;
		}

		if(diff_gc==false && bm->seg_type[target->seg_idx]==SUMMARY_SEG && 
		_PPS-bm->now_summary_seg->used_page_num >= target->validate_piece_num-target->invalidate_piece_num){
			diff_gc=true;
			if(bm->now_summary_seg->seg_idx!=target->seg_idx){
				free_block_num+=BPS;
			}
		}
		else if(bm->seg_type[target->seg_idx]==DATA_SEG){
			uint32_t seg_free_block_num=0;
			for (uint32_t i = 0; i < BPS; i++)
			{
				if (target->blocks[i]->is_full_invalid)
				{
					free_block_num++;
					seg_free_block_num++;
				}
			}
		}
		temp_seg_q.push(target->seg_idx);
		free(target);
	}
	res=true;

out:
	while(temp_seg_q.size()){
		uint32_t seg_idx=temp_seg_q.front();
		sm->insert_gc_target(sm, seg_idx);
		temp_seg_q.pop();
	}
	return res;
}

uint32_t gc_check_free_enable_space(L2P_bm *bm){
	blockmanager *sm=bm->segment_manager;
	std::queue<uint32_t> temp_seg_q;
	uint32_t res;

	uint32_t free_block_num = 0;
	bool diff_gc=false;
	while (1)
	{	
		__gsegment *target=sm->get_gc_target(sm);
		if(target==NULL){
			res=free_block_num;
			goto out;
		}

		if(diff_gc==false && bm->seg_type[target->seg_idx]==SUMMARY_SEG && 
		_PPS-bm->now_summary_seg->used_page_num >= target->validate_piece_num-target->invalidate_piece_num){
			diff_gc=true;
			if(bm->now_summary_seg->seg_idx!=target->seg_idx){
				free_block_num+=BPS;
			}
		}
		else if(bm->seg_type[target->seg_idx]==DATA_SEG){
			for (uint32_t i = 0; i < BPS; i++)
			{
				if (target->blocks[i]->is_full_invalid)
				{
					free_block_num++;
				}
			}
		}
		temp_seg_q.push(target->seg_idx);
		free(target);
	}

out:
	while(temp_seg_q.size()){
		uint32_t seg_idx=temp_seg_q.front();
		sm->insert_gc_target(sm, seg_idx);
		temp_seg_q.pop();
	}
	return res;
}
