#include "./exact_mapping.h"
#include "../../../include/debug_utils.h"
#include <stdlib.h>

map_function* exact_map_init(uint32_t contents_num, float fpr){
	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=exact_insert;
	res->query=exact_query;
	res->oob_check=map_default_oob_check;
	res->query_retry=exact_query_retry;
	res->query_done=map_default_query_done;
	res->make_done=exact_make_done;
	res->show_info=NULL;
	res->free=exact_free;

	exact_map *ex_map=(exact_map*)malloc(sizeof(exact_map));
	ex_map->map=(uint32_t*)malloc(sizeof(uint32_t) * contents_num);
	memset(ex_map->map, -1, sizeof(uint32_t) * contents_num);

	ex_map->now_map_num=0;
	ex_map->max_map_num=contents_num;

	res->private_data=(void*)ex_map;
	return res;
}

void exact_insert(map_function *m, uint32_t lba, uint32_t offset){
	exact_map *ex_map=(exact_map*)m->private_data;
	if(ex_map->now_map_num >= ex_map->max_map_num){
		EPRINT("over range", true);
	}
	ex_map->now_map_num++;
	ex_map->map[lba%ex_map->max_map_num]=offset;
}

uint32_t exact_query(map_function *m, request *req, map_read_param **param){
	exact_map *ex_map=(exact_map*)m->private_data;
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->p_req=req;
	res_param->mf=m;
	res_param->prev_offset=0;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;
	uint32_t target_offset=req->key%ex_map->max_map_num;
	if(ex_map->map[target_offset]==UINT32_MAX){
		return NOT_FOUND;
	}
	return ex_map->map[target_offset];
}

uint32_t exact_query_retry(map_function *m, map_read_param *param){
	exact_map *ex_map=(exact_map*)m->private_data;
	uint32_t target_offset=param->p_req->key%ex_map->max_map_num;
	if(ex_map->map[target_offset]==UINT32_MAX){
		return NOT_FOUND;
	}
	return ex_map->map[target_offset];
}

void exact_make_done(map_function *m){
	return;
}

void exact_free(map_function *m){
	exact_map *ex_map=(exact_map*)m->private_data;
	free(ex_map->map);
	free(ex_map);
	free(m);
}
