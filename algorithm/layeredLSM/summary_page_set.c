#include "./summary_page_set.h"
#include "../../include/debug_utils.h"
#define extract_spi(spm) (summary_page_iter*)((spm)->private_data)
sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *spm_set, uint32_t prefetch_num){
	sp_set_iter *res=(sp_set_iter*)calloc(1, sizeof(sp_set_iter));
	res->max_STE_num=max_STE_num;
	res->prefetch_num=prefetch_num;
	res->spm_set=spm_set;
	res->type=SPI_SET;

	res->read_STE_num=MIN(max_STE_num, prefetch_num);
	for(uint32_t i=0; i<res->read_STE_num; i++){
		spi_init(&res->spm_set[i]);
	}

	return res;
}

sp_set_iter *sp_set_iter_init_mf(map_function *mf){
	if(mf->type!=TREE_MAP){
		EPRINT("invalid map type", true);
	}
	sp_set_iter *res=(sp_set_iter*)calloc(1, sizeof(sp_set_iter));
	res->max_STE_num=CEIL(mf->now_contents_num, NORMAL_CUR_END_PTR);
	res->mf=mf;
	res->type=MF_SET;
	res->miter=mf->iter_init(mf);
	return res;
}

summary_pair sp_set_iter_pick(sp_set_iter *ssi){
	summary_pair res;
	if(ssi->type==SPI_SET){
		summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
		summary_page_iter *spi=extract_spi(spm);
		res=spi_pick_pair(spi);
	}
	else{
		res=ssi->mf->iter_pick(ssi->miter);
	}
	return res;
}

bool sp_set_iter_move(sp_set_iter *ssi){
	if(ssi->type==SPI_SET){
		summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
		summary_page_iter *spi=extract_spi(spm);
		if(spi==NULL){
			GDB_MAKE_BREAKPOINT;
		}
		if(spi->iter_done_flag || spi_move_forward(spi)){
			ssi->now_STE_num++;
			if(ssi->read_STE_num < ssi->max_STE_num){
				spi_init(&ssi->spm_set[ssi->read_STE_num++]);
			}
			return true;
		}
		return false;
	}
	else{
		if(ssi->miter->iter_done_flag||
				ssi->mf->iter_move(ssi->miter) || ssi->miter->read_pointer % NORMAL_CUR_END_PTR==0){
			ssi->now_STE_num++;
			return true;
		}
		return false;
	}
}

bool sp_set_iter_done_check(sp_set_iter *ssi){
	if(ssi->type==SPI_SET){
		if(ssi->now_STE_num==ssi->max_STE_num){
			return true;
		}
		summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
		summary_page_iter *spi=extract_spi(spm);
		return spi->iter_done_flag;
	}
	else{
		return ssi->miter->iter_done_flag;
	}
}

void sp_set_iter_free(sp_set_iter *ssi){
	if(ssi->type==SPI_SET){
		for(uint32_t i=0; i<ssi->max_STE_num; i++){
			spi_free(extract_spi(&ssi->spm_set[i]));
		}
	}
	else{
		ssi->mf->iter_free(ssi->miter);	
	}
	free(ssi);
}
