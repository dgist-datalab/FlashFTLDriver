#include "./plr_mapping.h"

map_function *plr_map_init(uint32_t contents_num, float fpr){
	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=plr_map_insert;
	res->query=plr_map_query;
	res->oob_check=plr_oob_check;
	res->query_retry=plr_map_query_retry;
	res->query_done=map_default_query_done;
	res->make_done=plr_map_make_done;
	res->get_memory_usage=plr_get_memory_usage;
	res->show_info=NULL;
	res->free=plr_map_free;

	plr_map *pmap=(plr_map*)calloc(1, sizeof(plr_map));
	uint32_t range=fpr*100/2;
	pmap->plr_body=new PLR((uint64_t)SLOPE_BIT, range);

	res->private_data=(void*)pmap;
	return res;
}

uint32_t plr_map_insert(map_function *mf, uint32_t lba, uint32_t offset){
	plr_map *pmap=extract_plr(mf);
	if(map_full_check(mf)){
		EPRINT("over flow", true);
	}
	pmap->plr_body->insert(lba, offset/L2PGAP);
	map_increase_contents_num(mf);
	return INSERT_SUCCESS;
}

uint64_t plr_get_memory_usage(map_function *mf, uint32_t target_bit){
	plr_map *pmap=extract_plr(mf);
	return pmap->plr_body->memory_usage(target_bit);
}

uint32_t plr_map_query(map_function *mf, uint32_t lba, map_read_param **param){
	plr_map *pmap=extract_plr(mf);
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->lba=lba;
	res_param->mf=mf;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	res_param->retry_flag=NOT_RETRY;
	*param=res_param;

	uint32_t res=pmap->plr_body->get(lba) * L2PGAP;
	res_param->prev_offset=res;
	return res;
}

uint32_t plr_oob_check(map_function *mf, map_read_param *param){
	if(param->oob_set[param->intra_offset]==param->lba){
		return param->intra_offset;
	}else{
		for(uint32_t i=0; i<L2PGAP; i++){
			if(param->oob_set[i]==param->lba){
				return i;
			}
		}
	}
	return NOT_FOUND;
}

uint32_t plr_map_query_retry(map_function *mf, map_read_param *param){
	if(param->retry_flag==NORMAL_RETRY){
		return NOT_FOUND;
	}
	if(param->retry_flag==FORCE_RETRY){
		param->prev_offset=(param->prev_offset/L2PGAP)*L2PGAP-1;
	}
	else{
		uint32_t lba = param->lba;
		if (lba < param->oob_set[0])
		{
			param->prev_offset = (param->prev_offset / L2PGAP) * L2PGAP - 1;
		}
		else if (lba > param->oob_set[L2PGAP-1])
		{
			param->prev_offset = (param->prev_offset / L2PGAP + 1) * L2PGAP;
		}
	}
	param->retry_flag = NORMAL_RETRY;

	return param->prev_offset;
}

void plr_map_make_done(map_function *mf){
	plr_map *pmap=extract_plr(mf);
	pmap->plr_body->insert_end();
}

void plr_map_free(map_function *mf){
	plr_map *pmap=extract_plr(mf);
	pmap->plr_body->clear();
	delete pmap->plr_body;
	free(pmap);
	free(mf);
}
