#include "./summary_page_set.h"
#define extract_spi(spm) (summary_page_iter*)((spm)->private_data)
sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *spm_set, uint32_t prefetch_num){
	sp_set_iter *res=(sp_set_iter*)calloc(1, sizeof(sp_set_iter));
	res->max_STE_num=max_STE_num;
	res->prefetch_num=prefetch_num;
	res->spm_set=spm_set;

	res->read_STE_num=MIN(max_STE_num, prefetch_num);
	for(uint32_t i=0; i<res->read_STE_num; i++){
		spi_init(&res->spm_set[i]);
	}

	return res;
}

summary_pair sp_set_iter_pick(sp_set_iter *ssi){
	summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
	summary_page_iter *spi=extract_spi(spm);
	return spi_pick_pair(spi);
}

bool sp_set_iter_move(sp_set_iter *ssi){
	summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
	summary_page_iter *spi=extract_spi(spm);
	spi_move_forward(spi);
	if(spi->read_pointer==MAX_CUR_POINTER){
		ssi->now_STE_num++;
		if(ssi->read_STE_num < ssi->max_STE_num){
			spi_init(&ssi->spm_set[ssi->read_STE_num++]);
		}
		return true;
	}
	return false;
}

void sp_set_iter_free(sp_set_iter *ssi){
	for(uint32_t i=0; i<ssi->max_STE_num; i++){
		spi_free(extract_spi(&ssi->spm_set[i]));
	}
	free(ssi);
}
