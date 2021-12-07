#include "./mapping_function.h"
#include "./translation_functions/exact_mapping.h"
#include "./translation_functions/bf_mapping.h"

map_function *map_function_factory(uint32_t type, uint32_t contents_num, float fpr){
	map_function *res=NULL;
	switch(type){
		case EXACT:
			res=exact_map_init(contents_num, fpr);
			break;
		case BF:
			res=bf_map_init(contents_num, fpr);
			break;
		case GUARD_BF:
			break;
		case PLR:
			break;
	}
	return res;
}
