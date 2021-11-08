#ifndef SORTED_TABLE_H
#define SROTED_TABLE_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../include/container.h"
#include "../../include/settings.h"
#include "block_table.h"
#define CEILING(a,b) (a/b + (a%b?1:0))
#define MAX_SECTOR_IN_RC ((_PPB-1)*L2PGAP)
//#define EXTRACT_PPA(PSA) (PSA/L2PGAP)
typedef struct sorted_table_entry{
	uint32_t PBA; //mapping for RCI to PBA
}STE;

typedef struct sorted_table_array{
	uint64_t sid;
	uint32_t max_STE_num;
	uint32_t now_STE_num;
	uint32_t write_pointer; //physical_page granuality

	L2P_bm *bm;
	bool summary_write_alert; //for letting run know when it should issue summary block
	STE *pba_array;
}st_array;

/*
	Function: st_array_init
	-----------------------
		Returns a new sorted table array for managing LSA to PSA in Run
			assigns memory for STE
			set max number of RCIs in returning SA(st_array)
			initializes ETC

	max_sector_num: the amount of sectors in the target run
	bm: it is necessary to assign new empty RC to returned SA when it needs space to write.
 */
st_array *st_array_init(uint32_t max_sector_num, L2P_bm *bm);

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
*/
uint32_t st_array_summary_translation(st_array *sa);

/*
	Function: st_array_write_translation
	------------------------------------
		Returns PSA by translating write_pointer of sa

	sa: target sa
*/
uint32_t st_array_write_translation(st_array *sa);

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
