#include "./shortcut.h"
sc_master *shortcut_init(uint32_t max_shortcut_num, uint32_t lba_range){
	sc_master *res=(sc_master*)calloc(1, sizeof(sc_master));
	res->free_q=new std::list<uint32_t> ();
	res->sc_map=(uint8_t*)malloc(sizeof(uint8_t)* lba_range);
	memset(res->sc_map, NOT_ASSIGNED_SC, sizeof(uint8_t) * lba_range);
	res->max_shortcut_num=max_shortcut_num;
	res->info_set=(shortcut_info*)calloc(max_shortcut_num, sizeof(shortcut_info));

	for(uint32_t i=0; i< max_shortcut_num; i++){
		res->info_set[i].idx=i;
		res->free_q->push_back(i);
	}

	return res;
}

void shortcut_add_run(sc_master *sc, run *r){
	uint32_t sc_info_idx=sc->free_q->front();
	if(r->info){
		EPRINT("already assigned run", true);
	}

	sc_info *t_info=&sc->info_set[sc_info_idx];
	if(t_info->r){
		EPRINT("already assigned info", true);
	}
	sc->free_q->pop_front();
	r->info=t_info;
	t_info->r=r;
}

void shortcut_link_lba(sc_master *sc, run *r, uint32_t lba){
	sc_info *t_info=r->info;
	if(!t_info){
		EPRINT("no sc in run", true);
	}
	t_info->linked_lba_num++;
	sc->sc_map[lba]=t_info->idx;
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
	uint32_t sc_info_idx=sc->sc_map[lba];
	if(sc_info_idx==NOT_ASSIGNED_SC){
		EPRINT("not assigned lba", true);
	}
	return sc->info_set[sc_info_idx].r;
}

void shortcut_unlink_and_link_lba(sc_master *sc, run *r, uint32_t lba){
	run *old_r=shortcut_query(sc, lba);
	if(old_r->info->idx==r->info->idx) return;
	shortcut_unlink_lba(sc, old_r, lba);
	shortcut_link_lba(sc, r, lba);
}


void shortcut_release_sc_info(sc_master *sc, uint32_t idx){
	sc_info *t_info=&sc->info_set[idx];
	t_info->linked_lba_num=t_info->unlinked_lba_num=0;
	t_info->r=NULL;
	sc->free_q->push_back(idx);
}

void shortcut_free(sc_master *sc){
	delete sc->free_q;
	free(sc->sc_map);
	free(sc->info_set);
	free(sc);
}
