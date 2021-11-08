#include "bf_mapping.h"

map_function *bf_map_init(uint32_t contents_num, float fpr){
	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=bf_map_insert;
	res->query=bf_map_query;
	res->query_retry=bf_map_query_retry;
	res->make_done=bf_make_done;
	res->show_info=NULL;

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
	bf_set(map->bfm, map->set_of_bf[map->write_pointer++], lba);
}

uint32_t bf_map_query(map_function *mf, uint32_t lba){
	extract_map(map, mf);
	for(uint32_t i=0; i<map->write_pointer; i++){
		if(bf_check(map->bfm, &map->set_of_bf[i], lba)){
			return i;
		}
	}
	return NOT_FOUND
}

uint32_t bf_map_query_retry(map_function *mf, uint32_t lba,
		uint32_t prev_offset, uint32_t* oob_set){
	extract_map(map, mf);
	for(uint32_t i=prev_offset+1; i<map->write_pointer; i++){
		if(bf_check(map->bfm, &map->set_of_bf[i], lba)){
			return i;
		}
	}
	return NOT_FOUND
}

void bf_make_done(map_function *mf){
	return;
}

void bf_map_free(map_function *mf){
	extract_map(map, mf);
	bf_parameter_free(map->bfm);
	free(map->set_of_bf);
	free(map);
	free(mf);
}
