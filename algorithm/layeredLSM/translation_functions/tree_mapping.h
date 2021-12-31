#ifndef TREE_MAP_FUNCTION
#define TREE_MAP_FUNCTION
#include "../mapping_function.h"
#include "../../../include/debug_utils.h"
#include <map>

#define extract_tree(a) ((tree_map*)a->private_data)
#define tree_iter std::map<uint32_t, uint32_t>::iterator

typedef struct tree_map{
	std::map<uint32_t, uint32_t> *body;
}tree_map;

map_function*	tree_map_init(uint32_t contents_num, float fpr);
uint32_t		tree_insert(map_function *m, uint32_t lba, uint32_t offset);
uint32_t		tree_query(map_function *m, uint32_t lba, map_read_param ** param);
uint32_t		tree_query_retry(map_function *m, map_read_param *param);
//void			tree_make_summary(map_function *m ,char *data, uint32_t *start_lba, bool first);
uint64_t 		tree_get_memory_usage(map_function *m, uint32_t target_bit);
void			tree_make_done(map_function *m);
void			tree_free(map_function *m);

map_iter *		tree_iter_init(map_function *m);
summary_pair	tree_iter_pick(map_iter *miter);
bool			tree_iter_move(map_iter *miter);
void 			tree_iter_adjust(map_iter *miter, uint32_t lba);
void			tree_iter_free(map_iter *miter);
#endif
