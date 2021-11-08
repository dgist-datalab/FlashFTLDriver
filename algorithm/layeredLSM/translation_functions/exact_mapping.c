#include "./exact_mapping.h"
#include "../../../include/debug_utils.h"
#include <stdlib.h>

map_function* exact_map_init(uint32_t contents_num, float fpr){
	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=exact_insert;
	res->query=exact_query;
	res->query_retry=exact_query_retry;
	res->make_done=exact_make_done;
	res->show_info=NULL;
	res->free=exact_free;

	exact_map *ex_map=(exact_map*)malloc(sizeof(exact_map));
	ex_map->map=(uint32_t*)malloc(sizeof(uint32_t) * contents_num);
	memset(ex_map->map, -1, sizeof(uint32_t) * contents_num);

	ex_map->max_map_num=contents_num;

	res->private_data=(void*)ex_map;
	return res;
}

void exact_insert(map_function *m, uint32_t lba, uint32_t offset){
	exact_map *ex_map=(exact_map*)m->private_data;
	if(lba > ex_map->max_map_num){
		EPRINT("over range", true);
	}
	ex_map->map[lba]=offset;
}

uint32_t exact_query(map_function *m, uint32_t lba){
	exact_map *ex_map=(exact_map*)m->private_data;
	return ex_map->map[lba];
}

uint32_t exact_query_retry(map_function *m, uint32_t lba, 
		uint32_t prev_offset, uint32_t *oob_set){
	exact_map *ex_map=(exact_map*)m->private_data;
	return ex_map->map[lba];
}

void exact_make_done(map_function *m){
	return;
}

void exact_free(map_function *m){
	exact_map *ex_map=(exact_map*)m->private_data;
	free(ex_map->map);
	free(ex_map);
}
