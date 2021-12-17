#ifndef SUMMARY_PAGE
#define SUMMARY_PAGE
#include "../../include/settings.h"
#include "../../interface/interface.h"
#include "../../include/sem_lock.h"
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
	uint32_t intra_offset;
}summary_pair;

typedef struct summary_page{
	uint32_t write_pointer;
	value_set *value;
	char *body;
}summary_page;

typedef struct summary_page_meta{
	uint32_t lba;
	uint32_t ppa;
	uint32_t pr_type;
	void *private_data;
}summary_page_meta;

typedef struct summary_page_iterator{
	uint32_t read_pointer;
	summary_page_meta *spm;
	value_set *value;
	char *body;
	fdriver_lock_t read_done;
	bool read_flag;
	bool iter_done_flag;
}summary_page_iter;

#define for_each_sp_pair(sp, idx, p)\
	for(idx=0; idx<MAX_CUR_POINTER &&\
			(p=((summary_pair*)sp->body)[idx]).lba!=UINT32_MAX; ++idx)

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
value_set *sp_get_data(summary_page *sp);

/*
	Function: sp_find_psa
	--------------------
		return psa which is address of 
	sp:
 */
uint32_t sp_find_psa(summary_page *sp, uint32_t lba);

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
summary_page_iter* spi_init(summary_page_meta *sp);

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
	spi:
 */
void spi_move_forward(summary_page_iter*);

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
