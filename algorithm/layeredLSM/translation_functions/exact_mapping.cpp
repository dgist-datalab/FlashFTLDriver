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
	res->get_memory_usage=exact_get_memory_usage;
	res->show_info=NULL;
	res->free=exact_free;

	exact_map *ex_map=(exact_map*)malloc(sizeof(exact_map));
	ex_map->map=(uint32_t*)malloc(sizeof(uint32_t) * contents_num);
	memset(ex_map->map, -1, sizeof(uint32_t) * contents_num);

	res->private_data=(void*)ex_map;
	return res;
}

uint32_t exact_insert(map_function *m, uint32_t lba, uint32_t offset){
	exact_map *ex_map=(exact_map*)m->private_data;
	if(map_full_check(m)){
		EPRINT("over range", true);
	}
	if(ex_map->map[lba%m->max_contents_num]!=(uint32_t)-1){
		return ex_map->map[lba%m->max_contents_num];
	}
	ex_map->map[lba%m->max_contents_num]=offset;
	map_increase_contents_num(m);
	return INSERT_SUCCESS;
}

uint32_t exact_query(map_function *m, uint32_t lba, map_read_param **param){
	exact_map *ex_map=(exact_map*)m->private_data;
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->lba=lba;
	res_param->mf=m;
	res_param->prev_offset=0;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;
	uint32_t target_offset=lba%m->max_contents_num;
	if(ex_map->map[target_offset]==UINT32_MAX){
		return NOT_FOUND;
	}
	return ex_map->map[target_offset];
}

uint64_t exact_get_memory_usage(map_function *m, uint32_t target_bit){
	return (uint64_t)target_bit * m->now_contents_num;	
}
uint32_t exact_query_retry(map_function *m, map_read_param *param){
	exact_map *ex_map=(exact_map*)m->private_data;
	uint32_t target_offset=param->lba%m->max_contents_num;
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
