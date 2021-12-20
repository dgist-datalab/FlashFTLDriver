#include "./mapping_function.h"
#include "./translation_functions/exact_mapping.h"
#include "./translation_functions/bf_mapping.h"
#include "./translation_functions/plr_mapping.h"
#include "./translation_functions/bf_guard_mapping.h"
#include "./translation_functions/tree_mapping.c"

uint32_t map_query_by_req(struct map_function *m, request *req, map_read_param **param){
	uint32_t res=m->query(m, req->key, param);
	(*param)->p_req=req;
	return res;
}

map_function *map_function_factory(uint32_t type, uint32_t contents_num, float fpr, uint32_t lba_bit_num){
	map_function *res=NULL;
	switch(type){
		case EXACT:
			res=exact_map_init(contents_num, fpr);
			break;
		case BF:
			res=bf_map_init(contents_num, fpr);
			break;
		case GUARD_BF:
			res=bfg_map_init(contents_num, fpr, lba_bit_num);
			break;
		case PLR_MAP:
			res=plr_map_init(contents_num, fpr);
			break;
		case TREE_MAP:
			res=tree_map_init(contents_num, fpr);
			break;
	}
	map_init(res, type, contents_num, lba_bit_num);
	res->query_by_req=map_query_by_req;
	return res;
}
