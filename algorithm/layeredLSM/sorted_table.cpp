#include "sorted_table.h"
#include "./mapping_function.h"
#include "./piece_ppa.h"
#include "../../include/search_template.h"

#define get_global_idx(sa, ste, intra_offset) \
	((sa)->pba_array[(ste)].start_write_pointer+(intra_offset))
	
extern bool debug_flag;
sa_master *sa_m;

extern uint32_t test_key;
extern uint32_t test_key2;
extern uint32_t test_piece_ppa;

static inline void __check_debug(st_array *sa, uint32_t lba, uint32_t psa, uint32_t ste_num, uint32_t intra_offset){
#ifndef LSM_DEBUG
	return;
#endif
	uint32_t retrieve_psa=st_array_read_translation(sa, ste_num, intra_offset);
#ifdef LSM_DEBUG
	if (psa != retrieve_psa){
		//	GDB_MAKE_BREAKPOINT;//not be commented
		EPRINT("retrieve_error", false);
		st_array_read_translation(sa, ste_num, intra_offset);
	}
#endif
	if((lba==test_key || lba==test_key2)  || psa==test_piece_ppa){
		uint32_t recency=sa_m->sid_map[sa->sid].r->info->recency;
		printf("target insert (%u:%u:%u, %u:%u) sid,ste,intra (%u,%u,%u) recency:%u:\n",test_key, test_key2, lba, test_piece_ppa, psa, sa->sid, ste_num, intra_offset, recency);
		if(lba==test_key && psa==test_piece_ppa){
			//GDB_MAKE_BREAKPOINT;
		}
	//	run_print(r, false);
	}
}

void sorted_array_master_init(uint32_t total_run_num){
	sa_m=(sa_master*)malloc(sizeof(sa_master));
	sa_m->now_sid_info=0;
	sa_m->sid_queue=new std::queue<uint32_t>();
	for(uint32_t i=0; i<total_run_num; i++){
		sa_m->sid_queue->push(i);
	}
	sa_m->sid_map=(sid_info*)calloc(total_run_num, sizeof(sid_info));
	sa_m->total_run_num=total_run_num;

	fdriver_mutex_init(&sa_m->lock);
}

void sorted_array_master_free(){
	delete sa_m->sid_queue;
	free(sa_m->sid_map);
	free(sa_m);
}

static sid_info* __find_sid_info_iter(uint32_t sid){
	if(sa_m->total_run_num <=sid){
		return NULL;
	}
	fdriver_lock(&sa_m->lock);
	sid_info *res=&sa_m->sid_map[sid];
	fdriver_unlock(&sa_m->lock);
	return res;
}

static uint32_t __get_empty_sid_idx(){
	fdriver_lock(&sa_m->lock);
	uint32_t sa_idx=sa_m->sid_queue->front();
	sa_m->sid_queue->pop();
	fdriver_unlock(&sa_m->lock);
	return sa_idx;
}

static inline void __release_sid_info_idx(uint32_t idx){
	fdriver_lock(&sa_m->lock);
	memset(&sa_m->sid_map[idx], 0, sizeof(sid_info));
	sa_m->sid_queue->push(idx);
	fdriver_unlock(&sa_m->lock);
}

sid_info* sorted_array_master_get_info(uint32_t sid){
	return __find_sid_info_iter(sid);
}


sid_info* sorted_array_master_get_info_mapgc(uint32_t start_lba, uint32_t piece_ppa, uint32_t*intra_idx){
	for(uint32_t i=0; i<sa_m->total_run_num; i++){
		sid_info *temp=&sa_m->sid_map[i];
		if(!temp || !temp->sa) continue;
		int t_intra_idx;
		if(temp->r->type==RUN_LOG){
			st_array *sa=temp->sa;
			for(uint32_t j=0; j<sa->now_STE_num; j++){
				if(sa->sp_meta[j].piece_ppa==piece_ppa){
					if(sa->sp_meta[j].copied) break;
					*intra_idx=j;
					return temp;
				}
			}
		}
		else{
			t_intra_idx=spm_find_target_idx(temp->sa->sp_meta, temp->sa->now_STE_num, start_lba);
			if(t_intra_idx==-1) continue;
			if (temp->sa->sp_meta[t_intra_idx].piece_ppa == piece_ppa && temp->sa->sp_meta[t_intra_idx].copied==false){
				*intra_idx = t_intra_idx;
				return temp;
			}
		}

	}
	*intra_idx=0;
	return NULL;
}

st_array *st_array_init(run *r, uint32_t max_sector_num, L2P_bm *bm, bool pinning, map_param param){
	st_array *res=(st_array*)calloc(1, sizeof(st_array));
	res->bm=bm;
	res->max_STE_num=CEIL(max_sector_num, MAX_SECTOR_IN_BLOCK);
	res->pba_array=(STE*)malloc((res->max_STE_num+1) * (sizeof(STE)));
	//memset(res->pba_array, -1, (res->max_STE_num+1)*sizeof(STE));
	for(uint32_t i=0; i<res->max_STE_num+1; i++){
		res->pba_array[i].max_offset=UINT32_MAX;
		res->pba_array[i].start_write_pointer=UINT32_MAX;
		res->pba_array[i].mf=NULL;
		res->pba_array[i].PBA=UINT32_MAX;
	}

	sa_m->now_sid_info%=sa_m->total_run_num;
	res->sp_meta=(summary_page_meta*)calloc(res->max_STE_num+1, sizeof(summary_page_meta));
	for(uint32_t i=0; i<res->max_STE_num+1; i++){
		res->sp_meta[i].start_lba=UINT32_MAX;
	}

	res->now_STE_num=0;
	res->type=pinning?ST_PINNING: ST_NORMAL;
	if(res->type==ST_PINNING){
		res->pinning_data=(uint32_t *)malloc(max_sector_num * sizeof(uint32_t));
		res->gced_unlink_data=bitmap_init(max_sector_num);
	}
	else{
		res->pinning_data=NULL;
	}

	res->param=param;

	uint32_t info_idx=__get_empty_sid_idx();
	res->sid=info_idx;
	sid_info* temp=&sa_m->sid_map[info_idx];
	if(temp->sid!=0){
		EPRINT("alread assigned sid_info(idx):%u\n", true, temp->sid);
		EPRINT("alread assigned sid_info at run idx:%u\n", true, temp->r->run_idx);
	}
	temp->sid=res->sid; temp->sa=res; temp->r=r;

	res->mp_manager.now_ppa=UINT32_MAX;
	res->mp_manager.now_idx=0;
	res->mp_manager.target_value=NULL;
	return res;
}
extern uint32_t target_PBA;
void st_array_free(st_array *sa){
	for(uint32_t i=0; i<sa->now_STE_num; i++){
		if(sa->sp_meta[i].copied==false && invalidate_piece_ppa(sa->bm->segment_manager, sa->sp_meta[i].piece_ppa, true)
				==BIT_ERROR){
			EPRINT("invalidate map error! %u", true, sa->sp_meta[i].piece_ppa);
		}
		map_function *mf=sa->pba_array[i].mf;
		if(mf){
			mf->free(mf);
		}
		uint32_t PBA=sa->pba_array[i].PBA;
		if (sa->sp_meta[i].all_reinsert == false && sa->sp_meta[i].copied == false && PBA != UINT32_MAX)
		{
			if(sa->bm->PBA_map[PBA/_PPB].sid==sa->sid){
				if (sa->bm->PBA_map[PBA / _PPB].type == LSM_BLOCK_MIXED){
					sa->bm->PBA_map[PBA / _PPB].type = LSM_BLOCK_FRAGMENT;
				}
			}
		}
	}

	__release_sid_info_idx(sa->sid);

	while(sa->issue_STE_num!=sa->end_STE_num){}

	if(sa->type==ST_PINNING){
		free(sa->pinning_data);
		bitmap_free(sa->gced_unlink_data);
	}

	free(sa->sp_meta);
	free(sa->pba_array);
	free(sa);
}

uint32_t target_sid=UINT32_MAX;
uint32_t target_ste_idx=0;

static inline char *__set_type_to_char(uint32_t type){
	switch (type){
		case NORAML_PBA:
			return "NORMAL";
		case EMPTY_PBA:
			return "EMPTY";
		case TRIVIAL_MOVE_PBA:
			return "TRIVIAL_MOVE";
		case FRAGMENT_PBA:
			return "FRAG";
	}
	return NULL;
}

static inline void __check_sid(st_array * sa, uint32_t now_ste, uint32_t PBA, uint32_t type){
#ifndef LSM_DEBGU	
	return;
#endif
	if(target_sid==UINT32_MAX) return;
	if(sa->sid==target_sid && sa->now_STE_num==target_ste_idx){
		//DEBUG_CNT_PRINT(test, UINT32_MAX, __FUNCTION__, __LINE__);
		target_PBA=sa->pba_array[now_ste].PBA;
		block_info *get_info = &sa->bm->PBA_map[PBA / _PPB];
		printf("[%s] block_info sid:%u,ste_idx:%u\n", __set_type_to_char(type),  get_info->sid, get_info->intra_idx);
	}
	else if(PBA==target_PBA && target_PBA!=0){
		printf("[%s] target_changed %u-%u\n", __set_type_to_char(type), sa->sid, now_ste);		
	}
}

void st_array_set_now_PBA(st_array *sa, uint32_t PBA, uint32_t set_type){
	switch (set_type)
	{
	case NORAML_PBA:
		sa->pba_array[sa->now_STE_num].PBA=PBA;
		sa->pba_array[sa->now_STE_num].start_write_pointer=sa->global_write_pointer;
		L2PBm_make_map(sa->bm, sa->pba_array[sa->now_STE_num].PBA, sa->sid, 
				sa->now_STE_num);
		if (sa->pba_array[sa->now_STE_num].mf){
			EPRINT("already assigned map", true);
		}
		break;
	case EMPTY_PBA:	
		sa->pba_array[sa->now_STE_num].start_write_pointer=sa->global_write_pointer;
		break;
	case FRAGMENT_PBA:
		if(sa->pba_array[sa->now_STE_num].PBA!=UINT32_MAX){
			sa->pba_array[sa->now_STE_num].PBA = UINT32_MAX;
		}
		break;
	case TRIVIAL_MOVE_PBA:
		sa->pba_array[sa->now_STE_num].PBA=PBA;
		sa->pba_array[sa->now_STE_num].start_write_pointer=sa->global_write_pointer;
		if(L2PBm_is_nomral_block(sa->bm, sa->pba_array[sa->now_STE_num].PBA)){
			L2PBm_move_owner(sa->bm, sa->pba_array[sa->now_STE_num].PBA, sa->sid, sa->now_STE_num, LSM_BLOCK_NORMAL);
		}
		else if(L2PBm_get_block_type(sa->bm, sa->pba_array[sa->now_STE_num].PBA)==LSM_BLOCK_MIXED){
			L2PBm_move_owner(sa->bm, sa->pba_array[sa->now_STE_num].PBA, sa->sid, sa->now_STE_num, LSM_BLOCK_MIXED);
		}

		break;
	default:
		EPRINT("not allowed type", true);
		break;
	}
	__check_sid(sa, sa->now_STE_num, PBA, set_type);
}

int compare_intra(STE a, uint32_t target){
	if(a.start_write_pointer<= target && target<=a.start_write_pointer+a.max_offset){
		return 0;
	}
	if(a.start_write_pointer>target){
		return 1;
	}
	else{
		return -1;
	}
}
/*
static inline uint32_t get_run_chunk_by_intra_idx(st_array *sa, uint32_t intra_idx){
	if(sa->internal_fragmented==false){
		return intra_idx/MAX_SECTOR_IN_BLOCK;
	}
	uint32_t target_idx;
	bs_search(sa->pba_array, 0, sa->now_STE_num, intra_idx, compare_intra, target_idx);
	return target_idx;
}*/

uint32_t st_array_read_translation(st_array *sa, uint32_t ste_num, uint32_t intra_idx){
	uint32_t global_idx=get_global_idx(sa, ste_num, intra_idx);
	if(sa->type==ST_PINNING){
		if(bitmap_is_set(sa->gced_unlink_data, global_idx)){
			return UNLINKED_PSA;
		}

		if(sa->pba_array[ste_num].PBA!=UINT32_MAX){
			uint32_t physical_page_addr = sa->pba_array[ste_num].PBA;
			return physical_page_addr * L2PGAP + global_idx-sa->pba_array[ste_num].start_write_pointer;
		}
		else{
			return sa->pinning_data[global_idx];
		}
	}

	uint32_t physical_page_addr=sa->pba_array[ste_num].PBA;
	return physical_page_addr*L2PGAP+intra_idx;
}

uint32_t st_array_get_target_STE(st_array *sa, uint32_t lba){
	/*return UINT32_MAX when there is no STE including lba*/
	return spm_find_target_idx(sa->sp_meta, sa->now_STE_num, lba);
}


uint32_t st_array_convert_global_offset_to_psa(st_array *sa, uint32_t global_offset){
	if(sa->pinning_data){
		return sa->pinning_data[global_offset];
	}
	else{
		uint32_t ste_num=global_offset/MAX_SECTOR_IN_BLOCK;
		return sa->pba_array[ste_num].PBA*L2PGAP+global_offset%MAX_SECTOR_IN_BLOCK;
	}
}

uint32_t st_array_summary_translation(st_array *sa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}

	if(sa->mp_manager.now_idx==0){
		sa->mp_manager.now_ppa=L2PBm_get_map_ppa(sa->bm);
	}

	uint32_t res=sa->mp_manager.now_ppa*L2PGAP+sa->mp_manager.now_idx++;
	return res;
}

value_set* st_swp_write(st_array *sa, summary_write_param *swp, uint32_t *oob, bool force){
	if(sa->mp_manager.target_value==NULL){
		sa->mp_manager.target_value=inf_get_valueset(NULL,FS_MALLOC_W,PAGESIZE);
	}
	uint32_t idx=swp->spm->piece_ppa%L2PGAP;
	memcpy(&sa->mp_manager.target_value->value[(idx)*LPAGESIZE], swp->value, LPAGESIZE);
	sa->mp_manager.oob[idx]=swp->oob;
	if(force || sa->mp_manager.now_idx==L2PGAP){
		value_set *res=sa->mp_manager.target_value;
		sa->mp_manager.target_value=NULL;
		memcpy(oob, sa->mp_manager.oob, sizeof(uint32_t)*L2PGAP);
		sa->mp_manager.now_idx=0;
		sa->mp_manager.now_ppa=UINT32_MAX;
		return res;
	}
	else{
		st_array_summary_write_done(swp);
	}
	return NULL;
}

value_set* st_get_remain(st_array *sa, uint32_t *oob, uint32_t *ppa){
	if(sa->mp_manager.target_value==NULL) return NULL;
	value_set *res = sa->mp_manager.target_value;
	sa->mp_manager.target_value = NULL;
	memcpy(oob, sa->mp_manager.oob, sizeof(uint32_t) * L2PGAP);
	sa->mp_manager.now_idx = 0;

	*ppa=sa->mp_manager.now_ppa;

	sa->mp_manager.now_ppa = UINT32_MAX;
	return res;
}

uint32_t st_array_write_translation(st_array *sa){
	if(sa->summary_write_alert){
		EPRINT("it is write_summary_order", true);
	}

	if(sa->inblock_write_pointer%MAX_SECTOR_IN_BLOCK==0){
		//unalinged block write --> no need to set now_pba, but store the start PBA
		if(sa->unaligned_block_write==false){
			st_array_set_now_PBA(sa, L2PBm_pick_empty_PBA(sa->bm), NORAML_PBA);
		}
		else{
			sa->unaligned_block_PBA=L2PBm_pick_empty_PBA(sa->bm);
		}
	}

	uint32_t res;
	
	if(sa->unaligned_block_write==false){
		uint32_t run_chunk_idx=sa->now_STE_num;
		uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;
		res=physical_page_addr*L2PGAP+sa->inblock_write_pointer;
	}
	else{
		res=sa->unaligned_block_PBA*L2PGAP+sa->inblock_write_pointer;
	}
	
	return res;
}

void st_array_set_unaligned_block_write(st_array *sa){
	if(sa->unaligned_block_write==false){
		sa->unaligned_block_write=true;
		sa->unaligned_block_PBA=sa->pba_array[sa->now_STE_num].PBA;
	}
}

static inline void __st_insert_mf(st_array *sa, blockmanager *sm, uint32_t now_STE_num, uint32_t lba, uint32_t intra_offset){
	map_function* mf=sa->pba_array[now_STE_num].mf;
	uint32_t res=mf->insert(mf, lba, intra_offset);
	if(mf->type==TREE_MAP && res!=INSERT_SUCCESS){
		EPRINT("not allowed!", true);
	}
	else if(mf->type!=TREE_MAP && res!=INSERT_SUCCESS){
		EPRINT("same lba insert to run\n", true);
	}
}

static inline bool check_fragment(st_array *sa, uint32_t ste_num, uint32_t psa, uint32_t intra_offset){
	if(psa%MAX_SECTOR_IN_BLOCK==intra_offset){
		if(psa/L2PGAP/_PPB*_PPB == sa->pba_array[ste_num].PBA){
			return false;
		}
	}
	return true;
}

uint32_t __st_array_insert_pair(st_array *sa, uint32_t lba, uint32_t psa, bool trivial, bool reinsert){
if(sa->summary_write_alert){
		EPRINT("it is write_summary_order", true);
	}

	if(sa->now_STE_num >= sa->max_STE_num){
		EPRINT("sorted_table is full!", true);
	}

	if (sa->sp_meta[sa->now_STE_num].pr_type == NO_PR)
	{
		sa->sp_meta[sa->now_STE_num].private_data = (void *)sp_init();
		sa->sp_meta[sa->now_STE_num].pr_type = WRITE_PR;
	}
	else if (sa->sp_meta[sa->now_STE_num].pr_type == READ_PR)
	{
		EPRINT("not allowed", true);
	}

	if(sa->sp_meta[sa->now_STE_num].start_lba > lba){
		sa->sp_meta[sa->now_STE_num].start_lba=lba;
	}
	if(sa->sp_meta[sa->now_STE_num].end_lba < lba){
		sa->sp_meta[sa->now_STE_num].end_lba=lba;
	}

	summary_page *sp=(summary_page*)sa->sp_meta[sa->now_STE_num].private_data;
	sp_insert(sp, lba, psa);
	if (sa->pba_array[sa->now_STE_num].mf == NULL){
		sa->pba_array[sa->now_STE_num].mf=map_function_factory(sa->param, MAX_SECTOR_IN_BLOCK);
	}

	__st_insert_mf(sa, sa->bm->segment_manager, sa->now_STE_num, lba, sa->ghost_write_pointer);
	if(sa->type==ST_PINNING){	
		sa->pinning_data[sa->global_write_pointer]=psa;
		if (trivial == false){
			if (sa->ghost_write_pointer % MAX_SECTOR_IN_BLOCK == 0)
			{
				st_array_set_now_PBA(sa, psa / L2PGAP / _PPB * _PPB, EMPTY_PBA);
			}
			/*fragment block check*/
			if (check_fragment(sa, sa->now_STE_num, psa, sa->ghost_write_pointer))
			{
				L2PBm_block_fragment(sa->bm, psa / L2PGAP / _PPB * _PPB, sa->sid);
				st_array_set_now_PBA(sa, UINT32_MAX, FRAGMENT_PBA);
			}
		}
	}
	__check_debug(sa, lba, psa, sa->now_STE_num, sa->ghost_write_pointer);

	sa->global_write_pointer++;
	if(!reinsert){
		sa->inblock_write_pointer++; // it is increased if this process is not reinsert
		sa->ghost_write_pointer++;
		if(sa->unaligned_block_write && sa->inblock_write_pointer%MAX_SECTOR_IN_BLOCK==0){
			sa->inblock_write_pointer=0;
		}
	}
	else{
		sa->ghost_write_pointer++;
	}

	if(sa->ghost_write_pointer%MAX_SECTOR_IN_BLOCK==0){
		sa->summary_write_alert=true;
	}
	return 0;
}

uint32_t st_array_insert_pair(st_array *sa, uint32_t lba, uint32_t psa, bool trivial){
	return __st_array_insert_pair(sa, lba, psa, trivial, false);
}

uint32_t st_array_insert_pair_for_reinsert(st_array *sa, uint32_t lba, uint32_t psa, bool trivial){
	return __st_array_insert_pair(sa, lba, psa, trivial, true);
}

void st_array_copy_STE(st_array *sa, STE *ste, summary_page_meta *spm, map_function *mf, bool unlinked_data_copy){
	st_array_copy_STE_des(sa, ste, spm, sa->now_STE_num, mf, unlinked_data_copy);
	sa->global_write_pointer+=ste->max_offset+1;
	st_array_finish_now_PBA(sa, false);
	sa->now_STE_num++;
	//sa->now_STE_num++;
}

void st_array_copy_STE_des(st_array *sa, STE *ste,summary_page_meta *spm, uint32_t des_idx, map_function *mf, bool unlinked_data_copy){
	memcpy(&sa->sp_meta[des_idx], spm, sizeof(summary_page_meta));
	st_array_set_now_PBA(sa, ste->PBA, TRIVIAL_MOVE_PBA);
	sa->sp_meta[des_idx].unlinked_data_copy=true;

	uint64_t memory_usage_bit=ste->mf->memory_usage_bit;
	if(mf){
		ste->mf->free(ste->mf);
		sa->pba_array[des_idx].mf=mf;
	}
	else{
		sa->pba_array[des_idx].mf=ste->mf;
	}
	ste->mf=map_empty_copy(memory_usage_bit);
	
	sa->pba_array[des_idx].max_offset=ste->max_offset;

	spm->copied=true;
}

uint32_t st_array_force_skip_block(st_array *sa){
	if(sa->now_STE_num >= sa->max_STE_num){
		EPRINT("sorted_table is full!", true);
	}
	if(sa->sp_meta[sa->now_STE_num].pr_type==NO_PR){
		return 0;
	}
	else if(sa->sp_meta[sa->now_STE_num].pr_type==READ_PR){
		EPRINT("not allowed", true);
	}

	summary_page *sp=(summary_page*)sa->sp_meta[sa->now_STE_num].private_data;
	sp_insert(sp, UINT32_MAX, NOT_POPULATE_PSA);
	sa->summary_write_alert=true;
	return 1;
}

void st_array_update_pinned_info(st_array *sa, uint32_t ste_num, uint32_t intra_offset, uint32_t new_psa, uint32_t old_psa){
	uint32_t global_idx=get_global_idx(sa, ste_num, intra_offset);
	if(sa->type!=ST_PINNING){
		EPRINT("this function only called in pinned SA", true);
	}
	if(sa->pinning_data[global_idx]!=old_psa){
		EPRINT("different psa inserted", true);
	}
	sa->pinning_data[global_idx]=new_psa;
	L2PBm_block_fragment(sa->bm, new_psa/L2PGAP, sa->sid);
}

void st_array_unlink_bit_set(st_array *sa, uint32_t ste_num, uint32_t intra_offset, uint32_t old_psa){
	uint32_t global_idx=get_global_idx(sa, ste_num, intra_offset);
	if(sa->type!=ST_PINNING){
		EPRINT("this function only called in pinned SA", true);
	}
	if(sa->pinning_data[global_idx]!=old_psa){
		EPRINT("different psa inserted", true);
	}
	bitmap_set(sa->gced_unlink_data, global_idx);
	return;
}

summary_write_param* st_array_get_summary_param(st_array *sa, uint32_t piece_ppa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}

	if(sa->sp_meta[sa->now_STE_num].pr_type==NO_PR){
		EPRINT("swp check erorr", true);
	}

	sa->sp_meta[sa->now_STE_num].piece_ppa=piece_ppa;
	summary_page *sp=(summary_page*)sa->sp_meta[sa->now_STE_num].private_data;
	sa->sp_meta[sa->now_STE_num].sorted=sp->sorted;
	sa->summary_write_alert=false;

	summary_write_param *swp=(summary_write_param*)malloc(sizeof(summary_write_param));
	swp->idx=sa->now_STE_num;
	swp->sa=sa;
	swp->spm=&sa->sp_meta[sa->now_STE_num];
	swp->oob=sa->sp_meta[sa->now_STE_num].start_lba;
	st_array_finish_now_PBA(sa, sa->unaligned_block_write);

	sa->pba_array[sa->now_STE_num].mf->make_done(sa->pba_array[sa->now_STE_num].mf);

	swp->value=sp_get_data((summary_page*)(sa->sp_meta[sa->now_STE_num].private_data));
	sa->now_STE_num++;
	sa->issue_STE_num++;
	return swp;
}

void st_array_summary_write_done(summary_write_param *swp){
	st_array *sa=swp->sa;
	fdriver_lock(&sa_m->lock);
	sa->end_STE_num++;
	fdriver_unlock(&sa_m->lock);
	sp_free((summary_page*)sa->sp_meta[swp->idx].private_data);
	sa->sp_meta[swp->idx].pr_type=NO_PR;
	free(swp);
}