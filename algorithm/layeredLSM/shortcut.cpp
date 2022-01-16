#include "./shortcut.h"
extern uint32_t test_key;
sc_master *shortcut_init(uint32_t max_shortcut_num, uint32_t lba_range){
	sc_master *res=(sc_master*)calloc(1, sizeof(sc_master));
	res->free_q=new std::list<uint32_t> ();
	res->sc_map=(uint8_t*)malloc(sizeof(uint8_t)* lba_range);
	memset(res->sc_map, NOT_ASSIGNED_SC, sizeof(uint8_t) * lba_range);
	res->max_shortcut_num=max_shortcut_num;
	res->info_set=(shortcut_info*)calloc(max_shortcut_num, sizeof(shortcut_info));

	res->now_recency=0;

	for(uint32_t i=0; i< max_shortcut_num; i++){
		res->info_set[i].idx=i;
		res->free_q->push_back(i);
	}

	for(uint32_t i=0; i<MAX_SC_DIR_NUM; i++){
		sc_dir_init(&res->sc_dir[i], NOT_ASSIGNED_SC);
	}

	fdriver_mutex_init(&res->lock);
	return res;
}

void shortcut_add_run(sc_master *sc, run *r, uint32_t level_num){
	uint32_t sc_info_idx=sc->free_q->front();
	if(r->info){
		EPRINT("already assigned run", true);
	}

	sc_info *t_info=&sc->info_set[sc_info_idx];
	if(t_info->r){
		EPRINT("already assigned info", true);
	}
	sc->free_q->pop_front();
	t_info->recency=sc->now_recency++;
	t_info->level_idx=level_num;

	r->info=t_info;
	t_info->r=r;
}

void shortcut_add_run_merge(sc_master *sc, run *r, run **rset, 
		uint32_t merge_num){
	uint32_t sc_info_idx=sc->free_q->front();
	if(r->info){
		EPRINT("already assigned run", true);
	}

	sc_info *t_info=&sc->info_set[sc_info_idx];
	if(t_info->r){
		EPRINT("already assigned info", true);
	}
	sc->free_q->pop_front();
	t_info->recency=sc->now_recency++;

	r->info=t_info;
	t_info->r=r;
}

void shortcut_link_lba(sc_master *sc, run *r, uint32_t lba){
	sc_info *t_info=r->info;
	if(!t_info){
		EPRINT("no sc in run", true);
	}
	t_info->linked_lba_num++;

	static uint32_t temp_idx=UINT32_MAX;
	if(lba==1189210){
		temp_idx=t_info->idx;
		printf("target lba:%u -> sc_info:%u\n",lba, t_info->idx);
	}
/*
	sc->now_memory_usage-=sc_dir_memory_usage(&sc->sc_dir[lba/SC_PER_DIR]);
	sc_dir_insert_lba(&sc->sc_dir[lba/SC_PER_DIR], lba%SC_PER_DIR, t_info->idx);
	sc->now_memory_usage+=sc_dir_memory_usage(&sc->sc_dir[lba/SC_PER_DIR]);
*/
	sc->sc_map[lba]=t_info->idx;
	if(lba==test_key){
		printf("\t %u target map to info:%u,level:%u,run:%u\n",lba, t_info->idx,t_info->level_idx, r->run_idx);
	}
}

void shortcut_unlink_lba(sc_master *sc, run *r, uint32_t lba){
	if(sc->sc_map[lba]==NOT_ASSIGNED_SC){
		return;
	}

	sc_info *t_info=r->info;
	if(!t_info){
		EPRINT("no sc in run", true);
	}
	t_info->unlinked_lba_num++;
	sc->sc_map[lba]=NOT_ASSIGNED_SC;
}

run* shortcut_query(sc_master *sc, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t sc_info_idx=sc->sc_map[lba];
/*
	uint32_t sc_info_temp_idx=sc_dir_query_lba(&sc->sc_dir[lba/SC_PER_DIR], lba%SC_PER_DIR);
	if(sc_info_idx!=sc_info_temp_idx){
		EPRINT("error sc info idx", true);
	}
*/
	if(sc_info_idx==NOT_ASSIGNED_SC){
	//	EPRINT("not assigned lba", true);
		fdriver_unlock(&sc->lock);
		return NULL;
	}
	run *res=sc->info_set[sc_info_idx].r;
	fdriver_unlock(&sc->lock);
	return res;
}

static inline int32_t __get_recency_cmp(sc_info *a, sc_info *b){
	if(a->level_idx < b->level_idx){
		return 1;
	}
	else if(a->level_idx > b->level_idx){
		return -1; 
	}	
	return a->recency-b->recency;
}

bool shortcut_validity_check_lba(sc_master *sc, run *r, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx=sc->sc_map[lba];
	if(info_idx==NOT_ASSIGNED_SC){
		fdriver_unlock(&sc->lock);
		return true;
	}
	bool res=__get_recency_cmp(&sc->info_set[info_idx], r->info)<=0;
	fdriver_unlock(&sc->lock);
	return res;
}

bool shortcut_validity_check_by_value(sc_master *sc, run *r, uint32_t level, uint32_t recency, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx=sc->sc_map[lba];
	if(info_idx==NOT_ASSIGNED_SC){
		fdriver_unlock(&sc->lock);
		return true;
	}
	if(r->info->idx==info_idx){
		fdriver_unlock(&sc->lock);
		return true;
	}
	sc_info temp_info;
	temp_info.level_idx=level;
	temp_info.recency=recency;
	bool res=__get_recency_cmp(&sc->info_set[info_idx], &temp_info)<=0;
	fdriver_unlock(&sc->lock);
	return res;
}

void shortcut_unlink_and_link_lba(sc_master *sc, run *r, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx=sc->sc_map[lba];
	if(info_idx!=NOT_ASSIGNED_SC){
		if(sc->info_set[info_idx].now_compaction==false &&__get_recency_cmp(&sc->info_set[info_idx], r->info) > 0){
			EPRINT("most recent value should be added", true);
		}
	}

	run *old_r=shortcut_query(sc, lba);
	if(old_r){
		shortcut_unlink_lba(sc, old_r, lba);
	}
	shortcut_link_lba(sc, r, lba);
	fdriver_unlock(&sc->lock);
}

void shortcut_release_sc_info(sc_master *sc, uint32_t idx){
	sc_info *t_info=&sc->info_set[idx];
	t_info->linked_lba_num=t_info->unlinked_lba_num=0;
	t_info->r=NULL;
	sc->free_q->push_back(idx);
}

void shortcut_free(sc_master *sc){
	delete sc->free_q;

	for(uint32_t i=0; i<MAX_SC_DIR_NUM; i++){
		sc_dir_free(&sc->sc_dir[i]);
	}

	free(sc->sc_map);
	free(sc->info_set);
	free(sc);
}

bool shortcut_validity_check_and_link(sc_master* sc, run *src_r, run* des_r, uint32_t lba){
	fdriver_lock(&sc->lock);
	if(src_r==NULL){
		src_r=des_r;
	}
	uint32_t info_idx=sc->sc_map[lba];
	bool res=info_idx==NOT_ASSIGNED_SC;
	if(!res){
		if(sc->info_set[info_idx].now_compaction==true){
			res=true;
		}
		else if(__get_recency_cmp(&sc->info_set[info_idx], src_r->info)<=0){
			res=true;
		}
	}
	if(res){
		if (info_idx != NOT_ASSIGNED_SC)
		{
			run *old_r = sc->info_set[sc->sc_map[lba]].r;
			if (old_r)
			{
				shortcut_unlink_lba(sc, old_r, lba);
			}
		}
		shortcut_link_lba(sc, des_r, lba);
	}
	fdriver_unlock(&sc->lock);
	return res;
}

uint64_t shortcut_memory_usage(sc_master *sc){
	return sc->now_memory_usage;
}