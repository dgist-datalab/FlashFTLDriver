#include "sorted_table.h"
#include <map>

typedef struct sorted_table_manager{
	uint64_t now_sid; //next sid to be assigned
	std::map<uint64_t, st_array*> *map_info;//mapping for fast finding when it processes GC
}st_manager;

static st_manager *STM;

st_array *st_array_init(uint32_t max_sector_num, L2P_bm *bm){
	if(!STM){
		STM=(st_manager*)calloc(1, sizeof(st_manager));
		STM->map_info=new std::map<uint64_t, st_array*>();
	}

	st_array *res=(st_array*)calloc(1, sizeof(st_array));
	res->bm=bm;
	res->max_STE_num=CEILING(max_sector_num, MAX_SECTOR_IN_RC);
	res->pba_array=(STE*)malloc(res->max_STE_num * sizeof(STE));
	memset(res->pba_array, -1, res->max_STE_num);

	res->sid=STM->now_sid++;
	return res;
}

void st_array_free(st_array *sa){
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
	
	free(sa->pba_array);
	free(sa);
}

uint32_t st_array_read_translation(st_array *sa, uint32_t intra_idx){
	uint32_t run_chunk_idx=intra_idx/MAX_SECTOR_IN_RC;
	uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;
	return physical_page_addr*L2PGAP+intra_idx%MAX_SECTOR_IN_RC;
}

uint32_t st_array_summary_translation(st_array *sa){
	if(!sa->summary_write_alert){
		EPRINT("it is not write_summary_order", true);
	}
	uint32_t physical_page_addr=sa->pba_array[(sa->write_pointer-1)/MAX_SECTOR_IN_RC].PBA;
	sa->summary_write_alert=false;
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

	sa->write_pointer++;
	if(sa->write_pointer%MAX_SECTOR_IN_RC==0){
		sa->summary_write_alert=true;
	}
	return res;
}
