#include "./summary_page_set.h"
#include "../../include/debug_utils.h"
#define extract_spi(spm) (summary_page_iter*)((spm)->private_data)

sp_set_iter *__sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *spm_set){
	sp_set_iter *res=(sp_set_iter*)calloc(1, sizeof(sp_set_iter));
	res->max_STE_num=max_STE_num;
	res->spm_set=spm_set;
	res->type=SPI_SET;
	res->noncopy_idx=0;
	res->noncopy_max_idx=0;
	res->noncopy_start_lba=(uint32_t*)calloc(max_STE_num ,sizeof(uint32_t));
	res->noncopy_end_lba=(uint32_t*)calloc(max_STE_num ,sizeof(uint32_t));
	return res;
}

sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *spm_set, uint32_t prefetch_num){
	sp_set_iter *res=__sp_set_iter_init(max_STE_num, spm_set);
	res->prefetch_num=prefetch_num;
	res->read_STE_num=MIN(max_STE_num, prefetch_num);
	for(uint32_t i=0; i<res->read_STE_num; i++){
		spi_init(&res->spm_set[i]);
	}
	return res;
}

sp_set_iter *sp_set_iter_init_mf(uint32_t max_STe_num, summary_page_meta *spm_set, map_function *mf){
	if(mf->type!=TREE_MAP){
		EPRINT("invalid map type", true);
	}
	sp_set_iter *res=__sp_set_iter_init(max_STe_num, spm_set);
	res->mf=mf;
	res->type=MF_SET;
	res->miter=mf->iter_init(mf);
	return res;
}

summary_pair sp_set_iter_pick(sp_set_iter *ssi){
	summary_pair res={UINT32_MAX, UINT32_MAX};

	if(ssi->now_STE_num == ssi->max_STE_num){
		return res;
	}

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

void sp_set_iter_skip_lba(sp_set_iter *ssi, uint32_t lba, uint32_t end_lba){
	ssi->noncopy_start_lba[ssi->noncopy_max_idx]=lba;
	ssi->noncopy_end_lba[ssi->noncopy_max_idx++]=end_lba;
}


bool sp_set_noncopy_check(sp_set_iter *ssi, uint32_t lba, uint32_t *end_lba){
	if(ssi->noncopy_idx==ssi->noncopy_max_idx) return false;
	else{	
		if(ssi->noncopy_start_lba[ssi->noncopy_idx]==lba){
			*end_lba=ssi->noncopy_end_lba[ssi->noncopy_idx];
			ssi->noncopy_idx++;
			return true;
		}
		else{
			return false;
		}
	}
}

uint32_t sp_set_iter_move(sp_set_iter *ssi){
	if(ssi->type==SPI_SET){
		summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
		summary_page_iter *spi=extract_spi(spm);
		if(spi==NULL){
			EPRINT("SPI null!!", false);
		}
		if(spi->iter_done_flag || spi_move_forward(spi)){
			ssi->now_STE_num++;

			if (ssi->read_STE_num < ssi->max_STE_num)
			{
				spi_init(&ssi->spm_set[ssi->read_STE_num++]);
			}
			
		}
	}
	else{
		if(ssi->miter->iter_done_flag||
				ssi->mf->iter_move(ssi->miter)){
			ssi->now_STE_num++;
		}
		else{
			//the iterator moved one step;
			summary_pair temp=ssi->mf->iter_pick(ssi->miter);
			for(uint32_t i=ssi->now_STE_num; i<ssi->max_STE_num; i++){
				if (temp.lba == ssi->spm_set[ssi->now_STE_num + 1].start_lba){
					ssi->now_STE_num++;
				}
			}
		}
	}
	return ssi->now_STE_num;
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
	free(ssi->noncopy_start_lba);
	free(ssi->noncopy_end_lba);
	free(ssi);
}
