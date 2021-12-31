#ifndef SUMMARY_PAGE_SET_H
#define SUMMARY_PAGE_SET_H
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
	map_iter *miter;
	summary_page_meta *spm_set;

	uint32_t noncopy_idx;
	uint32_t noncopy_max_idx;
	uint32_t *noncopy_start_lba;
	uint32_t *noncopy_end_lba;
}sp_set_iter;

sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *sp_set, uint32_t prefetch_num);
sp_set_iter *sp_set_iter_init_mf(uint32_t max_STE_num, summary_page_meta *sp_set, map_function *mf);

summary_pair sp_set_iter_pick(sp_set_iter *ssi);
void sp_set_iter_skip_lba(sp_set_iter *ssi, uint32_t lba, uint32_t end);
bool sp_set_noncopy_check(sp_set_iter *ssi, uint32_t lba, uint32_t *end_lba);
uint32_t sp_set_iter_move(sp_set_iter *ssi);
bool sp_set_iter_done_check(sp_set_iter *ssi);
void sp_set_iter_free(sp_set_iter *ssi);

#endif
