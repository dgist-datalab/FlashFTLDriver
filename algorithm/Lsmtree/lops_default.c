#include "level.h"
#include "page.h"
#include "lsmtree.h"
#include <stdio.h>

extern int32_t SIZEFACTOR;
extern lsmtree LSM;
extern pm d_m;
#ifdef KVSSD
extern KEYT key_min,key_max;
#endif

ppa_t def_moveTo_fr_page(bool isgc){
	uint32_t res;
	if(isgc){
		if(LSM.bm->check_full(LSM.bm,d_m.active,MASTER_BLOCK)){	
			if(LSM.bm->check_full(LSM.bm,d_m.reserve,MASTER_BLOCK)){	
				change_new_reserve(DATA);
			}
			LSM.active_block=getRBlock(DATA);
		}
		else{
			LSM.active_block=getBlock(DATA);
		}
	}else{
		LSM.active_block=getBlock(DATA);
	}
#ifdef DVALUE
	res=LSM.active_block->now_ppa*NPCINPAGE;
	LSM.active_block->idx_of_ppa=0;
#else
	res=LSM.active_block->now_ppa;
#endif
	return res;
/*
	if(def_blk_fchk()){
		if(isgc){
			if(LSM.bm->check_full(LSM.bm,d_m.active,MASTER_BLOCK)){	
				if(LSM.bm->check_full(LSM.bm,d_m.reserve,MASTER_BLOCK)){	
					change_new_reserve(DATA);
				}
				LSM.active_block=getRBlock(DATA);
			}
			else{
				LSM.active_block=getBlock(DATA);
			}
		}
		else{
			LSM.active_block=getBlock(DATA);
		}
	}
#ifdef DVALUE
	else{
		if(LSM.active_block->idx_of_ppa){
			LSM.active_block->idx_of_ppa=0;
			LSM.active_block->ppage_idx++;
		}
	}
	ppa_t res=(LSM.active_block->ppa+LSM.active_block->ppage_idx)*NPCINPAGE;
	return res;
#endif
	return (LSM.active_block->ppa+LSM.active_block->ppage_idx);
	*/
}

ppa_t def_get_page(uint8_t plength, KEYT simul_key){
	ppa_t res=0;
	if(LSM.active_block->idx_of_ppa>=NPCINPAGE){
		abort();
	}
#ifdef DVALUE
	res=LSM.active_block->now_ppa;
	res*=NPCINPAGE;
	res+=LSM.active_block->idx_of_ppa;
	LSM.active_block->idx_of_ppa+=plength;
#else
	res=LSM.active_block->now_ppa;
#endif
	/*
	if(!LSM.active_block->bitset){
		printf("fuck!\n");
		abort();
	}*/
#ifdef EMULATOR
	lsm_simul_put(res,simul_key);
#endif
	validate_PPA(DATA,res);

	return res;
}

bool def_blk_fchk(){
	bool res=false;
	abort();
	return res;
}

void def_move_heap( level *des,  level *src){
	return;
}

bool def_fchk( level *input){
	if(input->idx<LSM.LEVELCACHING){
		int a=LSM.lop->get_number_runs(input);
		int b=input->idx==0?input->m_num-2:
			input->m_num/(LSM.size_factor)*(LSM.size_factor-1);
		if(a>=b){
			return true;
		}
		return false;
	}

	if(input->istier){

	}
	else{
		if(input->n_num>=((input->m_num/(LSM.size_factor)*(LSM.size_factor-1)))){
			return true;
		}
		else if(input->m_num>=100 && input->n_num>=(((input->m_num/(LSM.size_factor)*(LSM.size_factor-1)))*95/100)){
			return true;
		}
	}
	return false;

}

run_t *def_make_run(KEYT start, KEYT end, uint32_t pbn){
	run_t * res=(run_t*)calloc(sizeof(run_t),1);
	res->key=start;
	res->end=end;
	res->pbn=pbn;
	res->run_data=NULL;
	res->c_entry=NULL;
	
	res->wait_idx=0;
	return res;
}
