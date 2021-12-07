#include "bf_mapping.h"

map_function *bf_map_init(uint32_t contents_num, float fpr){
	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=bf_map_insert;
	res->query=bf_map_query;
	res->oob_check=map_default_oob_check;
	res->query_retry=bf_map_query_retry;
	res->query_done=map_default_query_done;
	res->make_done=bf_map_make_done;
	res->show_info=NULL;
	res->free=bf_map_free;

	bf_map *map=(bf_map*)malloc(sizeof(bf_map));
	map->bfm=bf_parameter_setting(contents_num, fpr);
	map->set_of_bf=(bloom_filter*)malloc(sizeof(bloom_filter) * contents_num);
	map->write_pointer=0;
	
	res->private_data=(void*)map;
	return res;
}

void bf_map_insert(map_function *mf, uint32_t lba, uint32_t offset){
	extract_map(map, mf);
	if(map->bfm->contents_num < map->write_pointer){
		EPRINT("data overflow", true);
	}
	bf_set(map->bfm, &map->set_of_bf[map->write_pointer++], lba);
}

uint32_t bf_map_query(map_function *mf, request *req, map_read_param **param){
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->p_req=req;
	res_param->mf=mf;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;

	extract_map(map, mf);
	for(uint32_t i=0; i<map->write_pointer; i++){
		if(bf_check(map->bfm, &map->set_of_bf[i], req->key)){
			res_param->prev_offset=i;
			return i;
		}
	}

	return NOT_FOUND;
}

uint32_t bf_map_query_retry(map_function *mf, map_read_param *param){
	extract_map(map, mf);
	uint32_t lba=param->p_req->key;
	for(uint32_t i=param->prev_offset+1; i<map->write_pointer; i++){
		if(bf_check(map->bfm, &map->set_of_bf[i], lba)){
			param->prev_offset=i;
			return i;
		}
	}
	return NOT_FOUND;
}

void bf_map_make_done(map_function *mf){
	return;
}

void bf_map_free(map_function *mf){
	extract_map(map, mf);
	bf_parameter_free(map->bfm);
	free(map->set_of_bf);
	free(map);
	free(mf);
}
