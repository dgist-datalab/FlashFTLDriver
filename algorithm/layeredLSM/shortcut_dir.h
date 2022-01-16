#ifndef __SC_DIR_H__
#define __SC_DIR_H__
#include <list>
#include "../../include/data_struct/bitmap.h"

#define SC_PER_DIR (512)
/*
typedef struct compressed_list{
	uint8_t allocated_num;
	uint32_t *list;
}compressed_list;
*/
typedef struct shortcut_directory{
	bitmap *bmap;
	std::list<uint32_t> *head_list;
}shortcut_dir;

typedef std::list<uint32_t>::iterator list_iter;

void sc_dir_init(shortcut_dir *target, uint32_t init_value);
void sc_dir_insert_lba(shortcut_dir *target, uint32_t offset, uint32_t sc_idx);
uint32_t sc_dir_query_lba(shortcut_dir *target, uint32_t offset);
static inline uint32_t sc_dir_memory_usage(shortcut_dir *target){
	return target->head_list->size()*5+SC_PER_DIR;
}
void sc_dir_free(shortcut_dir *target);
#endif