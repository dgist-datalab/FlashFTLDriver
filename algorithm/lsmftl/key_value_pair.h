#ifndef __KEY_VALUE_PAIR_H__
#define __KEY_VALUE_PAIR_H__
#include <stdint.h>
#include "../../include/settings.h"
#define LAST_KP_IDX (PAGESIZE/sizeof(key_ptr_pair)-1)
#define KP_IN_PAGE (PAGESIZE/sizeof(key_ptr_pair))
typedef struct key_ptr_pair{
	uint32_t lba;
	uint32_t piece_ppa;
} key_ptr_pair;

typedef struct key_value_pair{
	uint32_t lba; //oob
	char *data; //4KB value
} key_value_pair;

uint32_t kp_find_piece_ppa(uint32_t lba, char *page_data);

#define MAPINPAGE (PAGESIZE/sizeof(key_ptr_pair))
#endif
