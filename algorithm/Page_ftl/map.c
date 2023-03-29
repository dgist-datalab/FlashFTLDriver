#include "map.h"
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

extern uint32_t test_key;
extern algorithm page_ftl;
extern double cur_timestamp;

uint32_t *seg_ratio;
extern unsigned long req_num;
char gcur=0;
int *debug_gnum;

void page_map_create(){
	debug_gnum=(int*)calloc(sizeof(int), _NOS);
	printf("NOS: %ld\n", _NOS);
	seg_ratio=(uint32_t*)calloc((GNUMBER), sizeof(uint32_t));
	pm_body *p=(pm_body*)calloc(sizeof(pm_body),1);
	p->mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP*L2PGAP);
	for(int i=0;i<_NOP*L2PGAP; i++){
		p->mapping[i]=UINT_MAX;
	}
	p->active=(__segment**)malloc(sizeof(__segment*)*2);
	p->reserve=(__segment **)malloc(sizeof(__segment*)*4);
	/*	
	for (uint32_t i=0;i<GNUMBER-1;i++) { 
		p->reserve[i]=page_ftl.bm->get_segment(page_ftl.bm,true); //reserve for GC
	}
	*/	
	p->stat = (STAT*)calloc(sizeof(STAT), 1);
	p->stat->user_write=0;
	p->stat->gc_write=0;
	for (int i=0;i<6;i++) {
		p->stat->vr[i]=0.0;
		p->stat->erase[i]=0.0;
		p->stat->gsize[i]=0;
	}
	
	p->active[0]=page_ftl.bm->get_segment(page_ftl.bm,true);	//now active block for inserted request.
	p->active[1]=page_ftl.bm->get_segment(page_ftl.bm, true);
	p->stat->gsize[0]++;
	p->stat->gsize[1]++;
	debug_gnum[p->active[0]->seg_idx]=0;
	debug_gnum[p->active[1]->seg_idx]=1;

	page_ftl.algo_body=(void*)p; //you can assign your data structure in algorithm structure
	p->seg_timestamp = (unsigned long*)calloc(_NOS, sizeof(unsigned long));
	p->avg_segage=ULONG_MAX;
	p->tmp_avg=0;
	p->gc_seg=0;
	p->rb_lbas = rb_create();
	q_init(&(p->q_lbas), _PPS*L2PGAP*_NOS);
}

void print_stat() {
	pm_body *p = (pm_body*)page_ftl.algo_body;
	printf("\n======================================\n");
	printf("total WAF: %.3f\n", ((double)(p->stat->user_write+p->stat->gc_write)/(double)p->stat->user_write));
	for (int i=0;i<6;i++) {
		printf("=> group %d[%d]: %.3f (erase: %lu)\n", i, p->stat->gsize[i], p->stat->vr[i]/p->stat->erase[i], (unsigned long)p->stat->erase[i]);
	}
	printf("======================================\n");
}

uint32_t page_map_assign(KEYT* lba, uint32_t max_idx, int hc_cnt, unsigned long *timestamp){
	//printf("lba : %lu\n", lba);
	uint32_t res=0;
	res=get_ppa(lba, L2PGAP, hc_cnt, timestamp);
	pm_body *p=(pm_body*)page_ftl.algo_body;
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		p->stat->user_write++;
		//printf("key: %lu\n", t_lba);
		//uint32_t previous_ppa=p->mapping[t_lba];
		if(p->mapping[t_lba]!=UINT_MAX){
			/*when mapping was updated, the old one is checked as a inavlid*/
			invalidate_ppa(p->mapping[t_lba]);
		}
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
		return 1;
	}
}

uint32_t page_map_gc_update(KEYT *lba, uint32_t idx, uint32_t mig_count){
	//printf("mig_count: %d\n", mig_count);
	uint32_t res=0;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	if (mig_count > GNUMBER) printf("mig count problem is found in gc_update******\n");
	/*when the gc phase, It should get a page from the reserved block*/
retry:
	if (p->reserve[mig_count] == NULL) {
		//initialize migration group
		p->reserve[mig_count] = page_ftl.bm->jy_get_time_segment(page_ftl.bm, true, cur_timestamp);
		p->stat->gsize[mig_count+2]++;
		debug_gnum[p->reserve[mig_count]->seg_idx]=mig_count+2;
	}
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve[mig_count]);
	if (res==UINT32_MAX){
		__segment* tmp=p->reserve[mig_count];
		seg_ratio[mig_count+2]++;
		p->stat->gsize[mig_count+2]++;
		p->reserve[mig_count] = page_ftl.bm->jy_change_reserve(page_ftl.bm, p->reserve[mig_count], cur_timestamp);
		page_ftl.bm->free_segment(page_ftl.bm, tmp);
		debug_gnum[p->reserve[mig_count]->seg_idx]=mig_count+2;
		goto retry;
	}

	//uint32_t old_ppa, new_ppa;
	for(uint32_t i=0; i<idx; i++){
		p->stat->gc_write++;
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

int is_lba_hot(uint32_t lba) {
        pm_body *p = (pm_body*)page_ftl.algo_body;
        int status=0;
        Redblack cur_rb;
        node* nd;
        int res=-1;
        uint32_t tmp_lba;

        nd = q_enqueue_int_node(lba, p->q_lbas);
        if (nd == NULL) {
                //queue is full
                tmp_lba = q_pick_int(p->q_lbas);
                status = rb_find_int(p->rb_lbas, tmp_lba, &cur_rb);
                if (status==0) perror("first interval analyzer, why there is no node in redblack???\n");
                        //the victim lba was the only lba in the queue
                if (cur_rb->item == (void*)q_dequeue_node(p->q_lbas)) {
                        rb_delete_item(cur_rb, 0, 1);
                } else if (((node*)(cur_rb->item))->d.data != tmp_lba) {
                        perror("first interval analyzer\n");
                        abort();
                }
                nd = q_enqueue_int_node(lba, p->q_lbas);
        }

        status = rb_find_int(p->rb_lbas, lba, &cur_rb);
        if (status) {
                //this is hot lba
                res=0;
                cur_rb->item = (void*)nd;
        } else {
                res=1;
                rb_insert_int(p->rb_lbas, lba, (void*)nd);
        }
        return res;
}

void q_size_down(int nsize) {
	pm_body *p = (pm_body*)page_ftl.algo_body;
	Redblack cur_rb;
	uint32_t tmp_lba;
	int status=0;
	if (nsize < p->q_lbas->size) {
		for (int i=0;i<2;i++) {
			tmp_lba = q_pick_int(p->q_lbas);
	                status = rb_find_int(p->rb_lbas, tmp_lba, &cur_rb);
	                if (status==0) perror("q_size_Down, why there is no node in redblack???\n");
	                        //the victim lba was the only lba in the queue
	                if (cur_rb->item == (void*)q_dequeue_node(p->q_lbas)) {
	                        rb_delete_item(cur_rb, 0, 1);
	                } else if (((node*)(cur_rb->item))->d.data != tmp_lba) {
	                        perror("q_Size_down\n");
	                        abort();
                	}
			if (p->q_lbas->size==nsize) break;
		}
		p->q_lbas->m_size = p->q_lbas->size;
	} else p->q_lbas->m_size = nsize;
	if (p->q_lbas->m_size < p->q_lbas->size) {
		perror("q_size_Down err\n");
		abort();
	}
	
}


void page_map_free(){
	pm_body *p=(pm_body*)page_ftl.algo_body;
	free(p->mapping);
	free(seg_ratio);
	free(p->reserve);
	free(p);
	
}


