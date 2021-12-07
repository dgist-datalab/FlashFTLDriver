#include "sorted_table.h"
#include <map>

typedef struct sorted_table_manager{
	uint64_t now_sid; //next sid to be assigned
	std::map<uint64_t, st_array*> *map_info;//mapping for fast finding when it processes GC
}st_manager;

//static st_manager *STM;

st_array *st_array_init(uint32_t max_sector_num, L2P_bm *bm){
	st_array *res=(st_array*)calloc(1, sizeof(st_array));
	/*
	if(!STM){
		STM=(st_manager*)calloc(1, sizeof(st_manager));
		STM->map_info=new std::map<uint64_t, st_array*>();
	}
	res->sid=STM->now_sid++;
	*/
	res->bm=bm;
	res->max_STE_num=CEILING(max_sector_num, MAX_SECTOR_IN_RC);
	res->pba_array=(STE*)malloc(res->max_STE_num * sizeof(STE));
	memset(res->pba_array, -1, res->max_STE_num);

	res->sp_meta=(summary_page_meta*)calloc(res->max_STE_num, sizeof(summary_page_meta));
	res->sp_idx=0;
	return res;
}

void st_array_free(st_array *sa){
	/*
	std::map<uint64_t, st_array*>::iterator iter=STM->map_info->find(sa->sid);
	if(iter==STM->map_info->end()){
		EPRINT("cannot find target sid in map", true);
	}
	STM->map_info->erase(iter);
	if(STM->map_info->size()==0){
		delete STM->map_info;
		free(STM);
		STM=NULL;
	}
	*/
	EPRINT("sp_meta should be invalidate", false);
	free(sa->sp_meta);
	free(sa->pba_array);
	free(sa);
}

uint32_t st_array_read_translation(st_array *sa, uint32_t intra_idx){
	uint32_t run_chunk_idx=intra_idx/MAX_SECTOR_IN_RC;
	uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;
	return physical_page_addr*L2PGAP+intra_idx%MAX_SECTOR_IN_RC;
}

uint32_t st_array_summary_translation(st_array *sa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}
	uint32_t physical_page_addr=sa->pba_array[(sa->write_pointer-1)/MAX_SECTOR_IN_RC].PBA;
	return ((physical_page_addr+_PPB-1)*L2PGAP);
}

uint32_t st_array_write_translation(st_array *sa){
	if(sa->summary_write_alert){
		EPRINT("it is write_summary_order", true);
	}

	if(sa->write_pointer%MAX_SECTOR_IN_RC==0){
		sa->pba_array[sa->now_STE_num].PBA=L2PBm_pick_empty_PBA(sa->bm);
		L2PBm_make_map(sa->bm, sa->pba_array[sa->now_STE_num].PBA, sa->sid);
		sa->now_STE_num++;
	}

	uint32_t run_chunk_idx=sa->write_pointer/MAX_SECTOR_IN_RC;
	uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;

	uint32_t res=physical_page_addr*L2PGAP+sa->write_pointer%MAX_SECTOR_IN_RC;
	return res;
}

uint32_t st_array_insert_pair(st_array *sa, uint32_t lba, uint32_t psa){
	if(sa->sp_idx >= sa->max_STE_num){
		EPRINT("sorted_table is full!", true);
	}

	if(sa->sp_meta[sa->sp_idx].pr_type==NO_PR){
		sa->sp_meta[sa->sp_idx].private_data=(void *)sp_init();
		sa->sp_meta[sa->sp_idx].pr_type=WRITE_PR;
	}
	else if(sa->sp_meta[sa->sp_idx].pr_type==READ_PR){
		EPRINT("not allowed", true);
	}

	summary_page *sp=(summary_page*)sa->sp_meta[sa->sp_idx].private_data;
	sp_insert(sp, lba, psa);

	sa->write_pointer++;
	if(sa->write_pointer%MAX_SECTOR_IN_RC==0){
		sa->summary_write_alert=true;
	}
	return 0;
}

summary_write_param* st_array_get_summary_param(st_array *sa, uint32_t ppa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}

	if(sa->sp_meta[sa->sp_idx].pr_type==NO_PR){
		return NULL;
	}
	sa->sp_meta[sa->sp_idx].ppa=ppa;
	sa->summary_write_alert=false;

	summary_write_param *swp=(summary_write_param*)malloc(sizeof(summary_write_param));
	swp->idx=sa->sp_idx;
	swp->sa=sa;
	swp->value=sp_get_data((summary_page*)(sa->sp_meta[sa->sp_idx++].private_data));

	return swp;
}

void st_array_summary_write_done(summary_write_param *swp){
	st_array *sa=swp->sa;
	sp_free((summary_page*)sa->sp_meta[swp->idx].private_data);
	sa->sp_meta[swp->idx].pr_type=NO_PR;
	free(swp);
}

