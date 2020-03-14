#include "memory_manager.h"
#include "level.h"
#include "cache.h"
#include <stdlib.h>
#include <stdio.h>
extern lsmtree LSM;
mm_t* mem_init(int um,int lc_min,int r_min){
	mm_t *res=(mm_t*)malloc(sizeof(mm_t));
	res->max_memory=um;
	res->lev_least_memory=res->lev_cache_memory=lc_min;
	res->read_least_memory=res->read_cache_memory=r_min;
	res->usable_memory=um-lc_min-r_min;
	return res;
}

void mem_free(mm_t* mm){
	free(mm);
}
void mem_print(){
	printf("%d %d %d %d [all usable lev read]\n",LSM.mm->max_memory,LSM.mm->usable_memory,LSM.mm->lev_cache_memory,LSM.mm->read_cache_memory);
}
bool mem_update(mm_t *mm,lsmtree *lsm, bool more_level_cache){
	bool result=false;
	if(more_level_cache){
		if(mm->usable_memory-mm->read_least_memory>0 && mm->max_memory>mm->lev_cache_memory){
			mm->usable_memory--;
			mm->lev_cache_memory++;
			result=true;
		}else{
	//		printf("um:%d rm:%d mm:%d lcm:%d\n",mm->usable_memory,mm->read_least_memory,mm->max_memory,mm->lev_cache_memory);
		}
	}
	else{

		if(mm->usable_memory-mm->lev_least_memory>0 && mm->max_memory>mm->read_cache_memory){
			mm->usable_memory--;
			mm->read_cache_memory++;
			result=true;
		}
	}

	if(mm->lev_cache_memory+mm->read_cache_memory>mm->max_memory){
		if(more_level_cache){
			mm->read_cache_memory--;	
		}else{
			mm->lev_cache_memory--;
		}
	}
	lsm->lop->cache_size_update(lsm->disk[LEVELCACHING-1],mm->lev_cache_memory);
	cache_size_update(lsm->lsm_cache,mm->read_cache_memory);
	//mem_print();
	return result;
}
void mem_trim_lc(mm_t *mm,lsmtree *lsm, uint32_t size){
	mm->lev_cache_memory-=size;
	mm->usable_memory+=size;
	lsm->lop->cache_size_update(lsm->disk[LEVELCACHING-1],mm->lev_cache_memory);
	//mem_print();
}
