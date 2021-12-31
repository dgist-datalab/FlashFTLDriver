#include "./tree_mapping.h"
map_function*	tree_map_init(uint32_t contents_num, float fpr){
	map_function *res=(map_function*)calloc(1, sizeof(map_function));
	res->insert=tree_insert;
	res->query=tree_query;
	res->oob_check=map_default_oob_check;
	res->query_retry=tree_query_retry;
	res->query_done=map_default_query_done;
	//res->make_summary=tree_make_summary;
	res->make_done=tree_make_done;
	res->free=tree_free;
	res->show_info=NULL;
	res->get_memory_usage=tree_get_memory_usage;

	res->iter_init=tree_iter_init;
	res->iter_pick=tree_iter_pick;
	res->iter_move=tree_iter_move;
	res->iter_adjust=tree_iter_adjust;
	res->iter_free=tree_iter_free;

	tree_map *tr_map=(tree_map*)malloc(sizeof(tree_map));
	tr_map->body=new std::map<uint32_t, uint32_t>();

	res->private_data=(void *)tr_map;
	return res;
}

uint32_t			tree_insert(map_function *m, uint32_t lba, uint32_t offset){
	tree_map *tr_map=extract_tree(m);
	if(map_full_check(m)){
		EPRINT("over range", true);
	}
	tree_iter it=tr_map->body->find(lba);
	if(it==tr_map->body->end()){
		tr_map->body->insert(std::pair<uint32_t, uint32_t>(lba, offset));
		map_increase_contents_num(m);
		return INSERT_SUCCESS;
	}
	else{
		uint32_t old_offset=it->second;
		it->second=offset;
		return old_offset;
	}
}
uint64_t 		tree_get_memory_usage(map_function *m, uint32_t target_bit){
	return (uint64_t)(target_bit*2+PTR_BIT) * m->now_contents_num;
}

uint32_t		tree_query(map_function *m, uint32_t lba, map_read_param ** param){
	tree_map *tr_map=extract_tree(m);
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->lba=lba;
	res_param->mf=m;
	res_param->prev_offset=0;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;
	tree_iter it=tr_map->body->find(lba);
	if(it!=tr_map->body->end()){
		return it->second;
	}
	else{
		return NOT_FOUND;
	}
}

uint32_t		tree_query_retry(map_function *m, map_read_param *param){
	tree_map *tr_map=extract_tree(m);
	tree_iter it=tr_map->body->find(param->lba);
	if(it!=tr_map->body->end()){
		return it->second;
	}
	else{
		return NOT_FOUND;
	}
}

/*
void			tree_make_summary(map_function *m, char *data, 
		uint32_t *start_lba, bool first){
	tree_map *tr_map=extract_tree(m);
	tree_iter it=first?tr_map->body->begin():tr_map->body->find(m->make_summary_lba);
	if(it==tr_map->body->end()){
		EPRINT("not exist lba", true);
	}

	summary_pair *sp_list=(summary_pair*)data;
	for(uint32_t i=0; i<(PAGESIZE/(sizeof(uint32_t)*2)) && 
				it!=tr_map->body->end(); i++, it++){
		if(i==0){
			*start_lba=it->first;
		}
		sp_list[i].lba=it->first;
		sp_list[i].intra_offset=it->second;
	}
	
	m->make_summary_lba=it==tr_map->body->end()?UINT32_MAX:it->first;
}*/

void			tree_make_done(map_function *m){
	return;
}

void			tree_free(map_function *m){
	tree_map *tr_map=extract_tree(m);
	delete tr_map->body;
	free(tr_map);
	free(m);
}

#define iter_extract_tree(iter) extract_tree((iter->m))

map_iter *		tree_iter_init(map_function *m){
	tree_map *tr_map=extract_tree(m);
	map_iter *res=(map_iter*)malloc(sizeof(map_iter));
	res->read_pointer=0;
	res->m=m;
	res->private_data=malloc(sizeof(std::map<uint32_t, uint32_t>::iterator));
	res->iter_done_flag=false;
	*(tree_iter*)res->private_data=tr_map->body->begin();
	return res;
}

summary_pair	tree_iter_pick(map_iter *miter){
	tree_map *tr_map=iter_extract_tree(miter);
	static summary_pair temp_pair={UINT32_MAX, UINT32_MAX};
	tree_iter iter=*(tree_iter*)miter->private_data;
	if(iter==tr_map->body->end()){
		miter->iter_done_flag=true;
		return temp_pair;
	}
	else{
		return {iter->first, iter->second};
	}
}

bool			tree_iter_move(map_iter *miter){
	tree_map *tr_map=iter_extract_tree(miter);
	tree_iter iter=*(tree_iter*)miter->private_data;
	miter->read_pointer++;
	iter++;
	*(tree_iter*)miter->private_data=iter;
	if(iter==tr_map->body->end()){
		miter->iter_done_flag=true;
		return true;
	}
	else{
		return false;
	}
}

void 			tree_iter_adjust(map_iter *miter, uint32_t lba){
	tree_map *tr_map=iter_extract_tree(miter);
	tree_iter iter=*(tree_iter*)miter->private_data;
	tree_iter temp_iter=tr_map->body->find(lba);
	if(temp_iter==tr_map->body->end()){
		EPRINT("not found lba", true);
	}
	iter=temp_iter;
}

void			tree_iter_free(map_iter *miter){
	free(miter->private_data);
	free(miter);
}
