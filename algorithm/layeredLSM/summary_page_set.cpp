#include "./summary_page_set.h"
#include "../../include/debug_utils.h"
#define extract_spi(spm) (summary_page_iter*)((spm)->private_data)

extern uint32_t test_key;

sp_set_iter *__sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *spm_set, bool differ_map){
	sp_set_iter *res=(sp_set_iter*)calloc(1, sizeof(sp_set_iter));
	res->max_STE_num=max_STE_num;
	res->spm_set=spm_set;
	res->type=SPI_SET;
	res->noncopy_idx=0;
	res->noncopy_max_idx=0;
	res->noncopy_start_lba=(uint32_t*)calloc(max_STE_num ,sizeof(uint32_t));
	res->noncopy_end_lba=(uint32_t*)calloc(max_STE_num ,sizeof(uint32_t));
	res->noncopy_flag=(bool*)calloc(max_STE_num, sizeof(bool));
	res->differ_map=differ_map;

	res->prev_ppa=UINT32_MAX;
	res->prev_value=NULL;
	return res;
}

sp_set_iter *sp_set_iter_init(uint32_t max_STE_num, summary_page_meta *spm_set, uint32_t prefetch_num, bool differ_map){
	sp_set_iter *res=__sp_set_iter_init(max_STE_num, spm_set, differ_map);
	res->prefetch_num=prefetch_num;
	res->read_STE_num=MIN(max_STE_num, prefetch_num);
	for(uint32_t i=0; i<res->read_STE_num; i++){
		spi_init(&res->spm_set[i], res->prev_ppa, &res->prev_value);
		res->prev_ppa=res->spm_set[i].piece_ppa/L2PGAP;
	}
	return res;
}

sp_set_iter *sp_set_iter_init_mf(uint32_t max_STe_num, summary_page_meta *spm_set, uint32_t entry_num, map_function *mf, bool differ_map){
	if(mf->type!=TREE_MAP){
		EPRINT("invalid map type", true);
	}
	sp_set_iter *res=__sp_set_iter_init(max_STe_num, spm_set, differ_map);
	res->ste_per_ent=entry_num/max_STe_num;
	res->miter_pick_cnt=0;
	res->mf=mf;
	res->type=MF_SET;
	res->miter=mf->iter_init(mf);
	return res;
}
extern bool debug_flag;
summary_pair sp_set_iter_pick(sp_set_iter *ssi, run *r, uint32_t *ste_num, uint32_t *intra_idx){
	summary_pair res={UINT32_MAX, UINT32_MAX};

	if(ssi->type==SPI_SET){
		if (ssi->now_STE_num == ssi->max_STE_num)
		{
			return res;
		}
		summary_page_meta *spm=&ssi->spm_set[ssi->now_STE_num];
		summary_page_iter *spi=extract_spi(spm);
		res=spi_pick_pair(spi);
		if(r->st_body->type==ST_PINNING){
			//printf("pinning %u\n", res.lba);
			uint32_t global_intra_idx=ssi->now_STE_num * MAX_SECTOR_IN_BLOCK+spi->read_pointer;
			res.piece_ppa=st_array_read_translation(r->st_body, ssi->now_STE_num, spi->read_pointer);
			if(intra_idx){
				*intra_idx=spi->read_pointer;
			}
			if(ste_num){
				*ste_num=ssi->now_STE_num;
			}
		}
		else{
			uint32_t intra_offset=res.piece_ppa%MAX_SECTOR_IN_BLOCK;
			if(ste_num){
				*ste_num=ssi->now_STE_num;
			}
			if(intra_idx){
				*intra_idx=intra_offset;
			}
			res.piece_ppa=st_array_read_translation(r->st_body, ssi->now_STE_num, intra_offset);
		}
	}
	else{
		if (ssi->miter->iter_done_flag){
			return res;
		}
		res=ssi->mf->iter_pick(ssi->miter);
		if(intra_idx){
			*intra_idx=res.piece_ppa;
		}
		if(ste_num){
			*ste_num=UINT32_MAX;
		}
		res.piece_ppa=r->st_body->pinning_data[res.piece_ppa];
	}
	return res;
}

uint32_t sp_set_get_ste_num(sp_set_iter *ssi, uint32_t global_offset){
	if(ssi->type==SPI_SET) return ssi->now_STE_num;
	else{
		summary_pair res=ssi->mf->iter_pick(ssi->miter);
		return res.piece_ppa/MAX_SECTOR_IN_BLOCK;
	}
	return UINT32_MAX;
}

void sp_set_iter_skip_lba(sp_set_iter *ssi, uint32_t idx, uint32_t lba, uint32_t end_lba){
	ssi->noncopy_flag[idx]=true;
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

void sp_set_iter_move_ste(sp_set_iter *ssi, uint32_t ste_num, uint32_t end_lba){
	if(ssi->noncopy_flag[ste_num]==false){
		EPRINT("not matched ste", true);
	}

	if(ssi->type==SPI_SET){
		ssi->now_STE_num++;
		if (ssi->read_STE_num < ssi->max_STE_num)
		{

			spi_init(&ssi->spm_set[ssi->read_STE_num], ssi->prev_ppa, &ssi->prev_value);
			ssi->prev_ppa=ssi->spm_set[ssi->read_STE_num].piece_ppa/L2PGAP;
			ssi->read_STE_num++;
		}
	}
	else{
		summary_pair temp=ssi->mf->iter_pick(ssi->miter);
		while(temp.lba<=end_lba){
			ssi->miter_pick_cnt++;
			ssi->now_STE_num=ssi->miter_pick_cnt/ssi->ste_per_ent;
			ssi->mf->iter_move(ssi->miter);
			temp=ssi->mf->iter_pick(ssi->miter);
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

				spi_init(&ssi->spm_set[ssi->read_STE_num], ssi->prev_ppa, &ssi->prev_value);
				ssi->prev_ppa=ssi->spm_set[ssi->read_STE_num].piece_ppa/L2PGAP;
				ssi->read_STE_num++;
			}
			
		}
	}
	else{
		if(ssi->miter->iter_done_flag||
				ssi->mf->iter_move(ssi->miter)){
			ssi->now_STE_num++;
		}
		else{
			ssi->miter_pick_cnt++;
			if(ssi->now_STE_num!=ssi->max_STE_num){
				ssi->now_STE_num=ssi->miter_pick_cnt/ssi->ste_per_ent;
			}
			/*
			//the iterator moved one step;
			summary_pair temp=ssi->mf->iter_pick(ssi->miter);
			for(uint32_t i=ssi->now_STE_num; i<ssi->max_STE_num; i++){
				if (temp.lba == ssi->spm_set[ssi->now_STE_num + 1].start_lba){
					ssi->now_STE_num++;
				}
			}*/
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
	free(ssi->noncopy_flag);
	free(ssi->noncopy_start_lba);
	free(ssi->noncopy_end_lba);
	free(ssi);
}

summary_page_meta* sp_set_check_trivial_old_data(sp_set_iter *ssi){
	if(ssi->type!=SPI_SET){
		return NULL;
	}
	else{
		if(ssi->spm_set[ssi->now_STE_num].unlinked_data_copy){
			return &ssi->spm_set[ssi->now_STE_num];
		}
	}
	return NULL;
}
