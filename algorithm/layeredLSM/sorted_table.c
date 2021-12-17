#include "sorted_table.h"
#include "./piece_ppa.h"

typedef std::map<uint32_t, sid_info>::iterator sid_info_iter;

sa_master *sa_m;

void sorted_array_master_init(){
	sa_m=(sa_master*)malloc(sizeof(sa_master));
	sa_m->now_sid_info=0;
}

void sorted_array_master_free(){
	free(sa_m);
}

static inline sid_info_iter __find_sid_info_iter(uint32_t sid){
	sid_info_iter iter=sa_m->sid_map.find(sid);
	if(sa_m->sid_map.end()==iter){
		EPRINT("invalid sid error", true);
	}
	return iter;
}

sid_info sorted_array_master_get_info(uint32_t sid){
	sid_info_iter iter=__find_sid_info_iter(sid);
	return iter->second;
}

st_array *st_array_init(run *r, uint32_t max_sector_num, L2P_bm *bm, bool pinning){
	st_array *res=(st_array*)calloc(1, sizeof(st_array));
	res->bm=bm;
	res->max_STE_num=CEIL(max_sector_num, MAX_SECTOR_IN_RC);
	res->pba_array=(STE*)malloc((res->max_STE_num+1) * (sizeof(STE)));
	memset(res->pba_array, -1, (res->max_STE_num+1)*sizeof(STE));

	res->sid=sa_m->now_sid_info++;
	res->sp_meta=(summary_page_meta*)calloc(res->max_STE_num+1, sizeof(summary_page_meta));
	res->sp_idx=0;
	res->type=pinning?ST_PINNING: ST_NORMAL;
	if(res->type==ST_PINNING){
		res->pinning_data=(uint32_t *)malloc(max_sector_num * sizeof(uint32_t));
	}
	else{
		res->pinning_data=NULL;
	}

	sid_info temp; temp.sid=res->sid; temp.sa=res; temp.r=r;
	sa_m->sid_map.insert(std::pair<uint32_t, sid_info>(res->sid, temp));

	return res;
}

void st_array_free(st_array *sa){
	EPRINT("sp_meta should be invalidate", false);
	for(uint32_t i=0; i<sa->now_STE_num; i++){
		if(sa->sp_meta[i].ppa==0){
			GDB_MAKE_BREAKPOINT;
		}
		if(invalidate_ppa(sa->bm->segment_manager, sa->sp_meta[i].ppa, true)
				==BIT_ERROR){
			EPRINT("invalidate map error!", true);
		}
	}

	sid_info_iter iter=__find_sid_info_iter(sa->sid);
	sa_m->sid_map.erase(iter);

	free(sa->pinning_data);
	free(sa->sp_meta);
	free(sa->pba_array);
	free(sa);
}

uint32_t st_array_read_translation(st_array *sa, uint32_t intra_idx){
	if(sa->type==ST_PINNING){
		return sa->pinning_data[intra_idx];
	}
	uint32_t run_chunk_idx=intra_idx/MAX_SECTOR_IN_RC;
	uint32_t physical_page_addr=sa->pba_array[run_chunk_idx].PBA;
	return physical_page_addr*L2PGAP+intra_idx%MAX_SECTOR_IN_RC;
}

uint32_t st_array_summary_translation(st_array *sa, bool force){
	if(!sa->summary_write_alert && !force){
		EPRINT("it is not write_summary_order", true);
	}
	return L2PBm_get_map_ppa(sa->bm)*L2PGAP;
}

uint32_t st_array_write_translation(st_array *sa){
	if(sa->type==ST_PINNING){
		EPRINT("not allowed in ST_PINNING", true);
	}
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
		sa->sp_meta[sa->sp_idx].lba=lba;
	}
	else if(sa->sp_meta[sa->sp_idx].pr_type==READ_PR){
		EPRINT("not allowed", true);
	}

	summary_page *sp=(summary_page*)sa->sp_meta[sa->sp_idx].private_data;
	sp_insert(sp, lba, psa);
	if(sa->type==ST_PINNING){
		sa->pinning_data[sa->write_pointer]=psa;
	}

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
	swp->spm=&sa->sp_meta[sa->sp_idx];
	swp->oob[0]=sa->sid;
	swp->oob[1]=sa->sp_idx;
	swp->value=sp_get_data((summary_page*)(sa->sp_meta[sa->sp_idx++].private_data));
	return swp;
}

void st_array_summary_write_done(summary_write_param *swp){
	st_array *sa=swp->sa;
	sp_free((summary_page*)sa->sp_meta[swp->idx].private_data);
	sa->sp_meta[swp->idx].pr_type=NO_PR;
	free(swp);
}

