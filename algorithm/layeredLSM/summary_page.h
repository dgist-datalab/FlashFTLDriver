#ifndef SUMMARY_PAGE
#define SUMMARY_PAGE
#include "../../include/settings.h"
#include "../../interface/interface.h"
#include "../../include/sem_lock.h"
#include "../../include/data_struct/bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define MAX_CUR_POINTER (PAGESIZE/sizeof(summary_pair))
#define MAX_IDX_SP (MAX_CUR_POINTER-1)
#define NORMAL_CUR_END_PTR (_PPB*L2PGAP)
#define NORMAP_CUR_END_IDX (_PPB*L2PGAP-1)

enum{
	NO_PR, WRITE_PR, READ_PR,
};

typedef struct summary_pair{
	uint32_t lba;
	uint32_t piece_ppa;
}summary_pair;

typedef struct summary_page{
	uint32_t write_pointer;
	char *value;
	bool sorted;
	char *body;
}summary_page;

typedef struct summary_page_meta{
	uint32_t start_lba;
	uint32_t end_lba;
	uint32_t piece_ppa;
	uint32_t pr_type;
	uint32_t original_level;
	uint32_t original_recency;
	bool sorted;
	bool unlinked_data_copy;
	bool copied;
	bool all_reinsert;
	void *private_data;
}summary_page_meta;

typedef struct summary_page_iterator{
	uint32_t read_pointer;
	summary_page_meta *spm;
	value_set *value;
	char *body;
	bool lock_deallocate;
	fdriver_lock_t read_done;
	bool read_flag;
	bool iter_done_flag;
}summary_page_iter;

#define for_each_sp_pair(sp, idx, p)\
	for(idx=0; idx<MAX_CUR_POINTER &&\
			(p=((summary_pair*)sp->body)[idx]).lba!=UINT32_MAX; ++idx)


static summary_pair sp_get_pair(summary_page *sp, uint32_t idx){
	summary_pair *p=(summary_pair*)(&sp->body[idx*sizeof(summary_pair)]);
	return *p;
}

static inline uint32_t spm_find_target_idx(summary_page_meta *spm, uint32_t spm_num, uint32_t lba){
	int32_t s=0, e=spm_num-1, mid;
	while(s<=e){
		mid=(s+e)/2;
		if(spm[mid].start_lba==lba){
			return mid;
		}
		else if(spm[mid].start_lba > lba){
			e=mid-1;
		}
		else if(spm[mid].end_lba >= lba){
			return mid;
		}
		else{
			s=mid+1;
		}
	}
	return UINT32_MAX;
}

static inline uint32_t spm_joint_check(summary_page_meta *spm, uint32_t spm_num, summary_page_meta *target){
	int32_t s=0, e=spm_num-1, mid;
	while(s<=e){
		mid=(s+e)/2;
		if(spm[mid].start_lba==target->start_lba){
			return mid;
		}
		else if(spm[mid].start_lba < target->start_lba){
			if(spm[mid].end_lba >=target->start_lba){
				return mid;
			}
			s=mid+1;
		}
		else if(spm[mid].start_lba > target->start_lba){
			if(spm[mid].start_lba <=target->end_lba){
				return mid;
			}
			e=mid-1;
		}
	}	
	return UINT32_MAX;
}


static inline uint32_t spm_joint_check_debug(summary_page_meta *spm, uint32_t spm_num, summary_page_meta *target, uint32_t skip_idx){
	for(uint32_t i=0; i<spm_num; i++){
		if(i==skip_idx) continue;
		if(target->start_lba > spm[i].end_lba){
			continue;
		}
		if(target->end_lba < spm[i].start_lba){
			if(skip_idx==UINT32_MAX){
				break;
			}
			else{
				continue;
			}
		}
		return i;
	}
	return UINT32_MAX;
}
/*
	Function: sp_init
	----------------
		returns initialized summary page after allocating it
 */
summary_page *sp_init();

/*
	Function: sp_free
	----------------
		deallocated summary_page
	sp : deallocating target
 */
void sp_free(summary_page *sp);

/*
	Function: sp_reinit
	----------------
		re-initializing summary_page
	sp: re-initializing target
 */
void sp_reinit(summary_page *sp);

/*
	Function: sp_insert
	----------------
		inserting lba, psa into summary_page
		return true when it is full after inserting
	sp:
	lba: lba
	intra_offset: intra_offset
 */
bool sp_insert(summary_page *sp, uint32_t lba, uint32_t intra_offset);

/*
	Function: sp_insert_pair
	-----------------------
		inserting summary_pair(lba, psa) into summary_page
		return true when it is full after inserting
	sp:
	p: target summary pair
 */
bool sp_insert_spair(summary_page *sp, summary_pair p);

/*
	Function: sp_get_data
	--------------------
		return summary_page's value
	sp:
 */
char *sp_get_data(summary_page *sp);

/*
	Function: sp_find_psa
	--------------------
		return psa which is address of 
	sp:
 */
uint32_t sp_find_psa(summary_page *sp, uint32_t lba);

uint32_t sp_find_offset_by_value(char *data, uint32_t lba);

/*
	Function: sp_print_all
	--------------------
		printing all data in summary_page for debuging
	sp:
 */
void sp_print_all(summary_page *sp);


/*
	Function: spi_init
	--------------------
		return summary_page_iterator from summary_page
	sp: iterating target 
 */
summary_page_iter* spi_init(summary_page_meta *sp, uint32_t prev_ppa, value_set **value);

/*
	Function: spi_init_by_data
	--------------------
		return summary_page_iterator from physical_page data
		when the current point reaches end of data, it returns (UINT32_MAX, UINT32_MAX) pair;
	data: summary data from physical_page
 */
summary_page_iter* spi_init_by_data(char *data);

/*
	Function: spi_pick_pair
	--------------------
		return current summary_pair in summary_page_iter
	spi:
 */
summary_pair spi_pick_pair(summary_page_iter *spi);

/*
	Function: spi_move_forward
	--------------------
		move read pointer forward
		return true, if the iterator is end
	spi:
 */
bool spi_move_forward(summary_page_iter*);

/*
	Function: spi_move_backward
	--------------------
		move read pointer backward
	spi:
 */
void spi_move_backward(summary_page_iter*);

/*
	Function: spi_free
	--------------------
		deallocate summary_page_iter
	spi:
 */
void spi_free(summary_page_iter*);
#endif
