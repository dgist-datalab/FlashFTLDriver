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
		return INSERT_SUCCESS;
	}
	else{
		uint32_t old_offset=it->second;
		it->second=offset;
		return old_offset;
	}
}

uint32_t		tree_query(map_function *m, request *req, map_read_param ** param){
	tree_map *tr_map=extract_tree(m);
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->p_req=req;
	res_param->mf=m;
	res_param->prev_offset=0;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;
	tree_iter it=tr_map->body->find(req->key);
	if(it!=tr_map->body->end()){
		return it->second;
	}
	else{
		return NOT_FOUND;
	}
}

uint32_t		tree_query_retry(map_function *m, map_read_param *param){
	tree_map *tr_map=extract_tree(m);
	tree_iter it=tr_map->body->find(param->p_req->key);
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
