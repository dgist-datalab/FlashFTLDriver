#ifndef SUMMARY_PAGE_SET_H
#define SUMMARY_PAGE_SET_H
#include "./shortcut_dir.h"
#include "./sorted_table.h"
#include "./summary_page.h"
#include "./mapping_function.h"
enum{
	SPI_SET, MF_SET
};

typedef struct summary_page_set_iter{
	uint32_t max_STE_num;
	uint32_t now_STE_num;
	uint32_t read_STE_num;
	uint32_t prefetch_num;
	uint32_t type;

	map_function *mf;
	uint32_t ste_per_ent;
	uint32_t miter_pick_cnt;
	map_iter *miter;
	summary_page_meta *spm_set;
	sc_dir_dp *dp;

	bool differ_map;
	uint32_t noncopy_idx;
	uint32_t noncopy_max_idx;
	bool *noncopy_flag;
	uint32_t *noncopy_start_lba;
	uint32_t *noncopy_end_lba;

	uint32_t prev_ppa;
	value_set *prev_value;
}sp_set_iter;

sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *sp_set, uint32_t prefetch_num, bool differ_map);
sp_set_iter *sp_set_iter_init_mf(uint32_t max_STE_num, summary_page_meta *sp_set, uint32_t entry_num, map_function *mf, bool differ_map);

uint32_t sp_set_get_ste_num(sp_set_iter *ssi, uint32_t global_offset);
summary_pair sp_set_iter_pick(sp_set_iter *ssi, run *r, uint32_t *ste_num, uint32_t *intra_idx);
void sp_set_iter_skip_lba(sp_set_iter *ssi, uint32_t idx, uint32_t lba, uint32_t end);
bool sp_set_noncopy_check(sp_set_iter *ssi, uint32_t lba, uint32_t *end_lba);
uint32_t sp_set_iter_move(sp_set_iter *ssi);
bool sp_set_iter_done_check(sp_set_iter *ssi);
summary_page_meta* sp_set_check_trivial_old_data(sp_set_iter *ssi);
void sp_set_iter_free(sp_set_iter *ssi);
void sp_set_iter_move_ste(sp_set_iter *ssi, uint32_t ste_num, uint32_t lba);

#endif
