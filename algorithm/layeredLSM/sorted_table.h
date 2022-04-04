#ifndef SORTED_TABLE_H
#define SORTED_TABLE_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>

#include "../../include/container.h"
#include "../../include/settings.h"
#include "./block_table.h"
#include "./summary_page.h"
#include "../../include/data_struct/bitmap.h"
#include "./mapping_function.h"
#include "./run.h"
#include "../../include/sem_lock.h"

#define UNLINKED_PSA (UINT32_MAX-1)
#define NOT_POPULATE_PSA (UINT32_MAX-2)
#define MAX_SECTOR_IN_BLOCK ((_PPB)*L2PGAP)
//#define EXTRACT_PPA(PSA) (PSA/L2PGAP)
enum{
	ST_NORMAL, ST_PINNING
};

enum{
	NORAML_PBA, EMPTY_PBA, TRIVIAL_MOVE_PBA, FRAGMENT_PBA
};

enum{
	INSERT_NORMAL, INSERT_FRAGMENT
};

typedef struct sorted_table_entry{
	uint32_t PBA; //mapping for RCI to PBA
	uint32_t max_offset;
	uint32_t start_write_pointer;
	map_function *mf;
}STE;

typedef struct map_ppa_manager{
	uint32_t now_ppa;
	uint32_t now_idx;
	//char value[PAGESIZE];
	value_set *target_value;
	uint32_t oob[L2PGAP];
}map_ppa_manager;

typedef struct sorted_table_array{
	bool internal_fragmented;
	map_param param;

	uint32_t sid;
	uint32_t max_STE_num;
	uint32_t now_STE_num;
	volatile uint32_t issue_STE_num;
	volatile uint32_t end_STE_num;
	uint32_t inblock_write_pointer;
	uint32_t global_write_pointer; //physical_page granuality
	uint32_t ghost_write_pointer;
	uint32_t type;

	L2P_bm *bm;
	bool summary_write_alert; //for letting run know when it should issue summary block
	bool unaligned_block_write;
	uint32_t unaligned_block_PBA;
	STE *pba_array;

	uint32_t *pinning_data;
	bitmap *gced_unlink_data;

	map_ppa_manager mp_manager;

	summary_page_meta *sp_meta;
}st_array;

typedef struct summary_write_param{
	uint32_t idx;
	st_array *sa;
	//value_set *value;
	char *value;
	uint32_t oob;
	summary_page_meta *spm;
}summary_write_param;

typedef struct sid_info{
	uint32_t sid;
	st_array *sa;
	struct run *r;
}sid_info;

typedef struct sorted_array_master{
	sid_info *sid_map;
	std::queue<uint32_t> *sid_queue;
	uint32_t now_sid_info;
	uint32_t total_run_num;
	//map_ppa_manager mp_manager;
	fdriver_lock_t lock;
}sa_master;

void sorted_array_master_init(uint32_t total_run_num);

void sorted_array_master_free();

sid_info* sorted_array_master_get_info(uint32_t sidx);
sid_info* sorted_array_master_get_info_mapgc(uint32_t start_lba, uint32_t ppa, uint32_t*intra_idx);

/*
	Function: st_array_init
	-----------------------
		Returns a new sorted table array for managing LSA to PSA in Run
			assigns memory for STE
			set max number of RCIs in returning SA(st_array)
			initializes ETC
	
	r: run which have this st_array;
	max_sector_num: the amount of sectors in the target run
	bm: it is necessary to assign new empty RC to returned SA when it needs space to write.
	store_summary_page_flag: if it is true, st_array store inserted data in summary_page
	pinning: if it is set to true, all the PSA in the st are all pinned.
 */
st_array *st_array_init(run *r, uint32_t max_sector_num, L2P_bm *bm, bool pinning, map_param param);

/*
	Function: st_array_free
	-----------------------
		reclaim allocated memory 

	sa: the target of being freed
 */
void st_array_free(st_array *sa);

/*
	Function: st_array_read_translation
	-----------------------------------
		Returns PSA by translating LSA


	sa: target sa
	intra_idx: target idx in the run
*/
uint32_t st_array_read_translation(st_array *sa, uint32_t ste_num,uint32_t intra_idx);

uint32_t st_array_get_target_STE(st_array *sa, uint32_t lba);
uint32_t st_array_convert_global_offset_to_psa(st_array *sa, uint32_t global_offset);
void st_array_copy_STE(st_array *sa, STE *ste, summary_page_meta *spm, struct map_function* mf, bool unlinked_data_copy);

void st_array_copy_STE_des(st_array *sa, STE *ste, summary_page_meta *spm, uint32_t des_idx, struct map_function* mf, bool unlinked_data_copy);

/*
	Function: st_array_summary_translation
	--------------------------------------
		Returns PSA where to write summary address

	sa: target sa
	force: if it is set to true, it forcely return the ppa;
*/
uint32_t st_array_summary_translation(st_array *sa, bool force);

/*
	Function: st_array_write_translation
	------------------------------------
		Returns PSA by translating write_pointer of sa

	sa: target sa
*/
uint32_t st_array_write_translation(st_array *sa);


/*
 * Function: st_array_insert_pair
 * --------------------- 
 *		inserting lba and psa 
 *
 * sa: 
 * lba: target_lba
 * psa: target_psa
 * */
uint32_t st_array_insert_pair(st_array *sa, uint32_t lba, uint32_t psa, bool trivial);

uint32_t st_array_insert_pair_for_reinsert(st_array *sa, uint32_t lba, uint32_t psa, bool trivial);

uint32_t st_array_force_skip_block(st_array *sa);

void st_array_set_now_PBA(st_array *sa, uint32_t PBA, uint32_t set_type);

static inline void st_array_finish_now_PBA(st_array *sa, bool reinsert){
	sa->pba_array[sa->now_STE_num].max_offset=sa->inblock_write_pointer-1;
	if(sa->pba_array[sa->now_STE_num].max_offset!=(MAX_SECTOR_IN_BLOCK-1)){
		sa->internal_fragmented=true;
	}

	if(sa->unaligned_block_write==false){
		sa->inblock_write_pointer=0;
	}
	sa->ghost_write_pointer=0;
}

void st_array_set_unaligned_block_write(st_array *sa);

/*
 * Function: st_array_update_pinned_info
 * --------------------- 
 *		update pinned psa at intra_offset to new_psa
 *
 * sa: 
 * intra_offset: pointer for updating target
 * new_psa: new psa
 * old_psa: for debugging
 * */
void st_array_update_pinned_info(st_array *sa, uint32_t ste_num, uint32_t intra_offset, uint32_t new_psa, uint32_t old_psa);

/*
	Function:st_array_unlink_bit_set
	--------------------------------
		set bitmap of unlinked intra offset from gc

	sa:
	intra_offset:
	old_psa: for debugging
 */

void st_array_unlink_bit_set(st_array *sa, uint32_t ste_num, uint32_t intra_offset, uint32_t old_psa);

/*
 * Function: st_array_block_lock
 * --------------------- 
 *		prevent block from GC
 *
 * sa: 
 * idx: index of block to lock
 * */
void st_array_block_lock(st_array *sa, uint32_t idx);


/*
 * Function: st_array_get_summary_param
 * --------------------- 
 *		return summary page information to write
 *
 * sa: 
 * ppa:		the physical address of summary data
 * force:	if it is set to true, this function returns summary data 
 *			even if the data is not aligned.
 * */
summary_write_param *st_array_get_summary_param(st_array *sa, uint32_t ppa, bool force);

/*
 * Function: st_array_summary_write_done
 * --------------------- 
 *		free sp_meta which is written
 *
 * sa: 
 * sp_idx: target sp meta idx
 * */
void st_array_summary_write_done(summary_write_param *swp);

value_set *st_swp_write(st_array *sa, summary_write_param *swp, uint32_t *oob, bool force);
value_set *st_get_remain(st_array *sa, uint32_t *oob, uint32_t *ppa);


static bool st_check_swp(st_array *sa){
	if(sa->sp_meta[sa->now_STE_num].pr_type==NO_PR){
		return false;
	}
	return true;
}

#endif
