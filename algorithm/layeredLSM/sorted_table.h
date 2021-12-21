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

#define MAX_SECTOR_IN_BLOCK ((_PPB)*L2PGAP)
//#define EXTRACT_PPA(PSA) (PSA/L2PGAP)
enum{
	ST_NORMAL, ST_PINNING
};

typedef struct sorted_table_entry{
	uint32_t PBA; //mapping for RCI to PBA
}STE;

typedef struct sorted_table_array{
	uint64_t sid;
	uint32_t max_STE_num;
	uint32_t now_STE_num;
	uint32_t write_pointer; //physical_page granuality
	uint32_t type;

	L2P_bm *bm;
	bool summary_write_alert; //for letting run know when it should issue summary block
	STE *pba_array;

	uint32_t *pinning_data;
	bitmap *gced_unlink_data;

	summary_page_meta *sp_meta;
}st_array;

typedef struct summary_write_param{
	uint32_t idx;
	st_array *sa;
	value_set *value;
	uint32_t oob[L2PGAP];
	summary_page_meta *spm;
}summary_write_param;

typedef struct sid_info{
	uint32_t sid;
	st_array *sa;
	struct run *r;
}sid_info;

typedef struct sorted_array_master{
	std::map<uint32_t, sid_info> *sid_map;
	uint32_t now_sid_info;
}sa_master;

void sorted_array_master_init();

void sorted_array_master_free();

sid_info sorted_array_master_get_info(uint32_t sidx);

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
st_array *st_array_init(run *r, uint32_t max_sector_num, L2P_bm *bm, bool pinning);

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
uint32_t st_array_read_translation(st_array *sa, uint32_t intra_idx);

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
uint32_t st_array_insert_pair(st_array *sa, uint32_t lba, uint32_t psa);

/*
 * Function: st_array_update_pinned_info
 * --------------------- 
 *		update pinned psa at intra_offset to new_psa
 *
 * sa: 
 * intra_offset: pointer for updating target
 * new_psa: new psa
 * */
void st_array_update_pinned_info(st_array *sa, uint32_t intra_offset, uint32_t new_psa);

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


/*
	Function: __st_update_map
	-------------------------
		update RCI<->PBA mapping while it processes GC

	sid: sid where it has the 'from_pba'
	from_pba: GCed physical block
	to_pba: newly allocated physical block
 */
void __st_update_map(uint32_t sid, uint32_t from_pba, uint32_t to_pba);

#endif
