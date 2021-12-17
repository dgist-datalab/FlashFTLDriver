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
}sp_set_iter;

sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *sp_set, uint32_t prefetch_num);
sp_set_iter *sp_set_iter_init_mf(map_function *mf);

summary_pair sp_set_iter_pick(sp_set_iter *ssi);
bool sp_set_iter_move(sp_set_iter *ssi);
void sp_set_iter_free(sp_set_iter *ssi);

#endif
