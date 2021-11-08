#ifndef PAGE_ALIGNER_H
#define PAGE_ALIGNER_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../include/settings.h"
#include "../../include/debug_utils.h"
#include "../../interface/interface.h"
/*
	physical_page_buffer
	it is buffering 4KB user-request until the buffered size in it reaches physical page size 16KB
 */
typedef struct physical_page_buffer{
	uint32_t buffered_num;
	uint32_t LBA[L2PGAP];
	char value[PAGESIZE];
}pp_buffer;

/*
	Function: pp_init
	-----------------------
		Returns a new phusical_page_buffer
 */
pp_buffer *pp_init();

/*
	Function: pp_buffer
	-----------------------
		free allocated memory of physicla_page_buffer

	pp: deallocation target
 */
void pp_free(pp_buffer *pp);

/*
	Function: pp_insert_buffer
	-----------------------
		Returns true-> the 'pp' reaches full
		insert value for buffering to target physical_page_buffer

	pp: 
	lba: inserted lba 
	value: the data of lba
 */
bool pp_insert_value(pp_buffer *pp,uint32_t lba, char *value);

/*
	Function: pp_get_write_target
	-----------------------
		Returns aligned data
	pp: 
	force: returns now buffered data anyway
 */
value_set *pp_get_write_target(pp_buffer *pp, bool force);

/*
	Function: pp_find_value
	-----------------------
		Returns value of lba parameter, if the value does not exist, it returns NULL

	pp:
	lba: retrieve target lba
 */
char *pp_find_value(pp_buffer *pp, uint32_t lba);

/*
	Function: pp_reinit_buffer
	-----------------------
		clear already allocated pp after it issues buffer data to flash

	pp: reinit target
 */
void pp_reinit_buffer(pp_buffer *pp);
#endif
