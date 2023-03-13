#include "map.h"
#include "gc.h"
#include "model.h"
#include "midas.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

extern uint32_t test_key;
extern algorithm page_ftl;

uint32_t utilization = 0;
FILE *vFile;
//FILE *gFile;
//FILE *wFile;

G_INFO *G_info;
extern STAT *stat;

void page_map_create(){
	model_create(TIME_WINDOW);
	printf("NOS: %d\n", _NOS);
	pm_body *p=(pm_body*)calloc(sizeof(pm_body),1);
	p->mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP*L2PGAP);
	p->ginfo = (uint32_t*)malloc(sizeof(uint32_t)*_NOS);
	p->gcur = 0;
	for (int i=0;i<_NOS;i++) {
		p->ginfo[i] = UINT_MAX;
	}
	for(int i=0;i<_NOP*L2PGAP; i++){
		p->mapping[i]=UINT_MAX;
	}
	p->active=(__segment **)malloc(sizeof(__segment*)*GNUMBER);
	p->group = (queue**)malloc(sizeof(queue*)*MAX_G);
	
	p->gnum=GNUMBER;

	p->m = (midas*)malloc(sizeof(midas));
	p->m->config = (uint32_t*)calloc(sizeof(uint32_t),MAX_G);
	p->m->config[0]=_NOS-3;
	p->m->vr = (double*)malloc(sizeof(double)*MAX_G);
	p->m->status=false;
	p->m->time_window = TIME_WINDOW;
	q_init(&(p->active_q), 20);

	p->n = (naive*)malloc(sizeof(naive));
	p->n->naive_on=true;
	p->n->naive_start=0;
	p->n->naive_q = (queue*)page_ftl.bm->q_return(page_ftl.bm);
	/*	
	for (uint32_t i=0;i<GNUMBER-1;i++) { 
		p->reserve[i]=page_ftl.bm->get_segment(page_ftl.bm,true); //reserve for GC
	}
	*/	
	char name[32];
	//sprintf(name, "./valid_ratio/ran_WAF_%d", GNUMBER);
	//wFile = fopen(name, "w");
	//sprintf(name, "./valid_ratio/ran_block_num_%d", GNUMBER);
	//gFile = fopen(name, "w");
	//sprintf(name, "./valid_ratio/11_valid_%d", GNUMBER);
	//vFile = fopen(name, "w");

	sprintf(name, "./valid_ratio/valid_tmp");
	vFile = fopen(name, "w");


	setbuf(vFile, NULL);
	p->active[0]=page_ftl.bm->get_segment(page_ftl.bm,true); //now active block for inserted request.
	stat->g->gsize[0]++;
	page_ftl.algo_body=(void*)p; //you can assign your data structure in algorithm structure
	seg_assign_ginfo(p->active[0]->seg_idx, 0);
}

uint32_t seg_assign_ginfo(uint32_t seg_idx, uint32_t group_number) {
	pm_body *p = (pm_body*)page_ftl.algo_body;
	uint32_t tmp = p->ginfo[seg_idx];
	p->ginfo[seg_idx] = group_number;
	return tmp;
}

uint32_t seg_get_ginfo(uint32_t seg_idx) {
	pm_body *p = (pm_body*)page_ftl.algo_body;
	uint32_t res = p->ginfo[seg_idx];
	return res;
}

uint32_t page_map_assign(KEYT* lba, uint32_t max_idx){
	//printf("lba : %lu\n", lba);
	uint32_t res=0;
	stat->write+=4;
	stat->tmp_write+=4;
	res=get_ppa(lba, L2PGAP);
	pm_body *p=(pm_body*)page_ftl.algo_body;
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		//printf("key: %lu\n", t_lba);
		uint32_t previous_ppa=p->mapping[t_lba];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			invalidate_ppa(p->mapping[t_lba]);
		} else utilization++;
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
		if(t_lba==test_key){

		}
	//	DPRINTF("\tmap set : %u->%u\n", t_lba, p->mapping[t_lba]);
	}

	return res;
}

uint32_t page_map_pick(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	return res;
}


uint32_t page_map_trim(uint32_t lba){
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	res=p->mapping[lba];
	if(res==UINT32_MAX){
		return 0;
	}
	else{
		invalidate_ppa(res);
		p->mapping[lba]=UINT32_MAX;
		utilization--;
		return 1;
	}
}

uint32_t page_map_gc_update(KEYT *lba, uint32_t idx, uint32_t mig_count){
	//printf("mig_count: %d\n", mig_count);
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	if (mig_count > GNUMBER) printf("mig count problem is found in gc_update******\n");
	/*when the gc phase, It should get a page from the reserved block*/
	//if (mig_count == 0) group_idx = mig_count;
retry:
	//if (p->gcur < mig_count) {
	if (p->active[mig_count]==NULL) {
		//initialize migration group
		p->active[mig_count] = page_ftl.bm->get_segment(page_ftl.bm, true);
		seg_assign_ginfo(p->active[mig_count]->seg_idx, mig_count);
		stat->g->gsize[mig_count]++;
		++p->gcur;
	}
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->active[mig_count]);
	if (res==UINT32_MAX){
		__segment* tmp=p->active[mig_count];
		stat->g->gsize[mig_count]++;
		if (p->active_q->size) {
			p->active[mig_count] = (__segment*)q_dequeue(p->active_q);
			if (mig_count >= p->n->naive_start) {
				page_ftl.bm->reinsert_segment(page_ftl.bm, tmp->seg_idx);
			} else {
				page_ftl.bm->jy_add_queue(page_ftl.bm, p->group[mig_count], tmp);	
			}
		} else {
			if (mig_count >= p->n->naive_start) {
				p->active[mig_count] = page_ftl.bm->change_reserve(page_ftl.bm, p->active[mig_count]);
			} else {
				p->active[mig_count] = page_ftl.bm->get_segment(page_ftl.bm,true);
				page_ftl.bm->jy_add_queue(page_ftl.bm, p->group[mig_count], tmp);
			}
		}
		page_ftl.bm->free_segment(page_ftl.bm, tmp);
		seg_assign_ginfo(p->active[mig_count]->seg_idx, mig_count);
		goto retry;
	}

	uint32_t old_ppa, new_ppa;
	for(uint32_t i=0; i<idx; i++){
		KEYT t_lba=lba[i];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			//invalidate_ppa(p->mapping[t_lba]);
		}
		/*mapping update*/
		p->mapping[t_lba]=res*L2PGAP+i;
		if(t_lba==test_key){

		}
	}

/*
	for(uint32_t i=idx; i<L2PGAP; i++){
		invalidate_ppa(res*L2PGAP+idx);
	}
*/
	return res;
}

void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
	free(p->active);
	free(p);
	fclose(vFile);
	//fclose(gFile);
	//fclose(wFile);
}


