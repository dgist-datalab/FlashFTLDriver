#ifndef __SC_DIR_H__
#define __SC_DIR_H__
#include <list>
#include <map>
#include <vector>
#include "./shortcut.h"
#include "../../include/data_struct/bitmap.h"

#define SC_PER_DIR (256)
#define MAX_TABLE_NUM (SC_PER_DIR/5)
//#define MAX_TABLE_NUM (SC_PER_DIR)
/*
typedef struct compressed_list{
	uint8_t allocated_num;
	uint32_t *list;
}compressed_list;
*/
typedef struct shortcut_directory{
	bitmap *bmap;
	uint32_t map_num;
	uint8_t *table;
	uint32_t idx;
	//std::list<uint32_t> *head_list;
}shortcut_dir;

typedef struct shortcut_directory_dp{
	int32_t prev_offset;
	int32_t prev_dir_idx;
	int32_t prev_table_idx;
	shortcut_dir *target_dir;
	bool reinit;
} sc_dir_dp;

typedef struct shortcut_directory_dp_master{
	std::multimap<uint32_t, sc_dir_dp*> *dir_dp_tree;
	fdriver_lock_t lock;
}sc_dir_dp_master;

//typedef std::list<uint32_t>::iterator list_iter;

void sc_dir_dp_master_init();
void sc_dir_dp_master_free();

sc_dir_dp *sc_dir_dp_init(struct shortcut_master *sc, uint32_t lba);
uint32_t sc_dir_dp_get_sc(sc_dir_dp *dp, struct shortcut_master *sc, uint32_t lba);
uint32_t sc_dir_insert_lba_dp(shortcut_dir *target, struct shortcut_master *sc, 
uint32_t sc_idx, uint32_t start_idx, std::vector<uint32_t> *lba_set, bool unlink);
void sc_dir_dp_free(sc_dir_dp* dp);

void sc_dir_init(shortcut_dir *target,uint32_t idx, uint32_t init_value);
void sc_dir_insert_lba(shortcut_dir *target, uint32_t offset, uint32_t sc_idx);
uint32_t sc_dir_query_lba(shortcut_dir *target, uint32_t offset);
static inline uint32_t sc_dir_memory_usage(shortcut_dir *target){
	return target->map_num*5+SC_PER_DIR;
}
void sc_dir_free(shortcut_dir *target);
#endif
