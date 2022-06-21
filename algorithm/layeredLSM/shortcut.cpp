#include "./shortcut.h"
#include <math.h>
extern uint32_t test_key;
extern uint32_t test_key2;

#define SC_DIR(sc, lba) (&(sc)->sc_dir[(lba)/SC_PER_DIR])
#define SC_OFFSET(lba) ((lba)%SC_PER_DIR)

sc_master *shortcut_init(uint32_t max_shortcut_num, uint32_t lba_range, uint32_t lba_bit){
	sc_master *res=(sc_master*)calloc(1, sizeof(sc_master));
	res->free_q=new std::list<uint32_t> ();

	res->max_shortcut_num=max_shortcut_num;
	res->info_set=(shortcut_info*)calloc(max_shortcut_num, sizeof(shortcut_info));

	res->now_recency=0;

	for(uint32_t i=0; i< max_shortcut_num; i++){
		res->info_set[i].idx=i;
		res->free_q->push_back(i);
	}
#ifdef SC_MEM_OPT
	res->sc_dir=(shortcut_dir*)malloc(sizeof(shortcut_dir)*MAX_SC_DIR_NUM);
	
	for(uint32_t i=0; i<MAX_SC_DIR_NUM; i++){
		sc_dir_init(&res->sc_dir[i], i, NOT_ASSIGNED_SC);
		res->now_sc_memory_usage+=sc_dir_memory_usage(&res->sc_dir[i]);
	}
	sc_dir_dp_master_init();
	res->max_sc_memory_usage=(MAX_TABLE_NUM*5*MAX_SC_DIR_NUM+lba_range);
#else
	res->sc_map=(uint8_t*)malloc(sizeof(uint8_t)* lba_range);
	memset(res->sc_map, NOT_ASSIGNED_SC, sizeof(uint8_t) * lba_range);
	//res->max_memory_usage=lba_range*ceil(log2(max_shortcut_num));
#endif
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

#ifdef SC_MEM_OPT
	sc->now_sc_memory_usage-=sc_dir_memory_usage(&sc->sc_dir[lba/SC_PER_DIR]);	
	sc_dir_insert_lba(SC_DIR(sc, lba), SC_OFFSET(lba), t_info->idx);
	sc->now_sc_memory_usage+=sc_dir_memory_usage(&sc->sc_dir[lba/SC_PER_DIR]);
#else
	sc->sc_map[lba]=t_info->idx;
#endif

	if(lba==test_key || lba==test_key2){
		EPRINT_CNT(test,UINT32_MAX, "\t %u target map to info:%u,level:%u,run:%u\n",false, lba, t_info->idx,t_info->level_idx, r->run_idx);
	}
}

void shortcut_link_bulk_lba(sc_master *sc, run *r, std::vector<uint32_t> *lba_set, bool unlink){
	fdriver_lock(&sc->lock);
	sc_info *t_info=r->info;
#ifdef SC_MEM_OPT
	uint32_t idx=0;
	while(1){
		uint32_t lba=(*lba_set)[idx];
		uint32_t target_idx=lba/SC_PER_DIR;
		sc->now_sc_memory_usage-=sc_dir_memory_usage(&sc->sc_dir[target_idx]);	
		if(test_key==lba){
			printf("%u move target sc->%u\n", test_key, t_info->idx);
		}
#ifdef SC_QUERY_DP
		idx=sc_dir_insert_lba_dp(&sc->sc_dir[target_idx], sc, t_info->idx, idx, lba_set, unlink);
#else	
		for(idx; idx<lba_set->size() && (*lba_set)[idx]/SC_PER_DIR==target_idx; idx++){
			lba=(*lba_set)[idx];
			if(unlink){
				uint32_t info_idx=sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba));
				if(info_idx!=NOT_ASSIGNED_SC){
					sc->info_set[info_idx].unlinked_lba_num++;
				}
			}
			sc_dir_insert_lba(SC_DIR(sc, lba), SC_OFFSET(lba), t_info->idx);
			t_info->linked_lba_num++;
		}
#endif
		sc->now_sc_memory_usage+=sc_dir_memory_usage(&sc->sc_dir[target_idx]);
		if(idx==lba_set->size()){
			break;
		}
	}

#else
	std::vector<uint32_t>::iterator iter = lba_set->begin();
	for (; iter != lba_set->end(); iter++){
		uint32_t lba = *iter;
		t_info->linked_lba_num++;
		if(unlink){
			sc->info_set[sc->sc_map[lba]].unlinked_lba_num++;
		}
		sc->sc_map[lba] = t_info->idx;
		if (lba == test_key || lba == test_key2)
		{
			EPRINT_CNT(test, UINT32_MAX, "\t %u target map to info:%u,level:%u,run:%u\n", false, lba, t_info->idx, t_info->level_idx, r->run_idx);
		}
	}
#endif
	fdriver_unlock(&sc->lock);
}

void shortcut_unlink_lba(sc_master *sc, run *r, uint32_t lba){
#ifdef SC_MEM_OPT
	if(sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba)));
#else
	if(sc->sc_map[lba]==NOT_ASSIGNED_SC){
		return;
	}
#endif
	sc_info *t_info=r->info;
	if(!t_info){
		EPRINT("no sc in run", true);
	}
	t_info->unlinked_lba_num++;

#ifdef SC_MEM_OPT
	sc->now_sc_memory_usage-=sc_dir_memory_usage(&sc->sc_dir[lba/SC_PER_DIR]);	
	sc_dir_insert_lba(SC_DIR(sc, lba), SC_OFFSET(lba), NOT_ASSIGNED_SC);
	sc->now_sc_memory_usage+=sc_dir_memory_usage(&sc->sc_dir[lba/SC_PER_DIR]);	
#else
	sc->sc_map[lba]=NOT_ASSIGNED_SC;
#endif
}

run* shortcut_query(sc_master *sc, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t sc_info_idx;

#ifdef SC_MEM_OPT
	sc_info_idx=sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba));
#else
	sc_info_idx=sc->sc_map[lba];
#endif

	if(sc_info_idx==NOT_ASSIGNED_SC){
	//	EPRINT("not assigned lba", true);
		fdriver_unlock(&sc->lock);
		return NULL;
	}
	run *res=sc->info_set[sc_info_idx].r;
	fdriver_unlock(&sc->lock);
	return res;
}

#ifdef SC_QUERY_DP
run* shortcut_dp_query(sc_master *sc, sc_dir_dp *dp, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t sc_info_idx;
	sc_info_idx=sc_dir_dp_get_sc(dp, sc, lba);

	if(sc_info_idx==NOT_ASSIGNED_SC){
	//	EPRINT("not assigned lba", true);
		fdriver_unlock(&sc->lock);
		return NULL;
	}
	run *res=sc->info_set[sc_info_idx].r;
	fdriver_unlock(&sc->lock);
	return res;
}
#endif

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
	uint32_t info_idx;
#ifdef SC_MEM_OPT
	info_idx=sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba));
#else
	info_idx=sc->sc_map[lba];
#endif

	if(info_idx==NOT_ASSIGNED_SC){
		fdriver_unlock(&sc->lock);
		return true;
	}
	bool res=__get_recency_cmp(&sc->info_set[info_idx], r->info)<=0;
	fdriver_unlock(&sc->lock);
	return res;
}

#ifdef SC_QUERY_DP
bool shortcut_validity_check_dp_lba(sc_master *sc, run *r, sc_dir_dp *dp, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx;
	info_idx=sc_dir_dp_get_sc(dp, sc, lba);

	if(info_idx==NOT_ASSIGNED_SC){
		fdriver_unlock(&sc->lock);
		return true;
	}
	bool res=__get_recency_cmp(&sc->info_set[info_idx], r->info)<=0;
	fdriver_unlock(&sc->lock);
	return res;
}
#endif

bool shortcut_validity_check_by_value(sc_master *sc, run *r, uint32_t level, uint32_t recency, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx;
#ifdef SC_MEM_OPT
	info_idx=sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba));
#else
	info_idx=sc->sc_map[lba];
#endif

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
#ifdef SC_QUERY_DP
bool shortcut_validity_check_by_dp_value(sc_master *sc, run *r, sc_dir_dp *dp, uint32_t level, uint32_t recency, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx;
	info_idx=sc_dir_dp_get_sc(dp, sc, lba);

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
#endif

void shortcut_unlink_and_link_lba(sc_master *sc, run *r, uint32_t lba){
	fdriver_lock(&sc->lock);
	uint32_t info_idx;
#ifdef SC_MEM_OPT
	info_idx=sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba));
#else
	info_idx=sc->sc_map[lba];
#endif
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
#ifdef SC_MEM_OPT
	for(uint32_t i=0; i<MAX_SC_DIR_NUM; i++){
		sc_dir_free(&sc->sc_dir[i]);
	}
	sc_dir_dp_master_free();
#else
	free(sc->sc_map);
#endif
	free(sc->info_set);
	free(sc);

}

bool shortcut_validity_check_and_link(sc_master* sc, run *src_r, run* des_r, uint32_t lba){
	fdriver_lock(&sc->lock);
	if(src_r==NULL){
		src_r=des_r;
	}
	uint32_t info_idx;
#ifdef SC_MEM_OPT
	info_idx=sc_dir_query_lba(SC_DIR(sc, lba), SC_OFFSET(lba));
#else
	info_idx=sc->sc_map[lba];
#endif
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
			run *old_r = sc->info_set[info_idx].r;
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

#ifdef SC_QUERY_DP
bool shortcut_validity_check_and_link_dp(sc_master* sc, sc_dir_dp *dp, run *src_r, run* des_r, uint32_t lba){
	fdriver_lock(&sc->lock);
	if(src_r==NULL){
		src_r=des_r;
	}
	uint32_t info_idx=sc_dir_dp_get_sc(dp, sc, lba);

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
			run *old_r = sc->info_set[info_idx].r;
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
#endif

uint64_t shortcut_memory_usage(sc_master *sc){
	return sc->now_sc_memory_usage;
}
