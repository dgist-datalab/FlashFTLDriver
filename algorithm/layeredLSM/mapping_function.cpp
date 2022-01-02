#include "./mapping_function.h"
#include "./translation_functions/exact_mapping.h"
#include "./translation_functions/bf_mapping.h"
#include "./translation_functions/plr_mapping.h"
#include "./translation_functions/bf_guard_mapping.h"
#include "./translation_functions/tree_mapping.h"

uint32_t map_query_by_req(struct map_function *m, request *req, map_read_param **param){
	uint32_t res=m->query(m, req->key, param);
	(*param)->p_req=req;
	return res;
}

map_function *map_function_factory(map_param param, uint32_t contents_num){
	map_function *res=NULL;
	switch(param.map_type){
		case EXACT:
			res=exact_map_init(contents_num, param.fpr);
			break;
		case BF:
			res=bf_map_init(contents_num, param.fpr);
			break;
		case GUARD_BF:
			res=bfg_map_init(contents_num, param.fpr, param.lba_bit);
			break;
		case PLR_MAP:
			res=plr_map_init(contents_num, param.fpr);
			break;
		case TREE_MAP:
			res=tree_map_init(contents_num, param.fpr);
			break;
	}
	map_init(res, param.map_type, contents_num, param.lba_bit);
	res->query_by_req=map_query_by_req;
	res->moved=false;
	res->memory_usage_bit=0;
	return res;
}

uint64_t map_memory_per_ent(uint32_t type, uint32_t target_bit, float fpr){
	switch(type){
		case GUARD_BF:
			return (uint64_t)find_sub_member_num(fpr, 10000, target_bit);
			break;
		default:
			break;
	}
	return 48;
}

void emptry_free(map_function *mf){
	free(mf);
}

map_function *map_empty_copy(uint64_t _memory_usage_bit){
	map_function *res=(map_function*)calloc(1,sizeof(map_function));
	res->memory_usage_bit=_memory_usage_bit;
	res->free=emptry_free;
	return res;
}