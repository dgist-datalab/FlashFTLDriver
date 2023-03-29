#include "gc.h"
#include "map.h"
#include "../../include/data_struct/list.h"
#include <stdlib.h>
#include <stdint.h>

extern uint32_t *seg_ratio;
uint32_t gc_norm_count=0;
extern double cur_timestamp;
extern unsigned long long req_num;
extern int* debug_gnum;

extern algorithm page_ftl;
void invalidate_ppa(uint32_t t_ppa){
	/*when the ppa is invalidated this function must be called*/
	page_ftl.bm->unpopulate_bit(page_ftl.bm, t_ppa);
}

void validate_ppa(uint32_t ppa, KEYT *lbas, uint32_t max_idx, uint32_t mig_count, unsigned long *time_stamp){
	/*when the ppa is validated this function must be called*/
	for(uint32_t i=0; i<max_idx; i++){
		page_ftl.bm->populate_bit(page_ftl.bm,ppa * L2PGAP+i);
	}

	/*this function is used for write some data to OOB(spare area) for reverse mapping*/
	int len=sizeof(KEYT)*L2PGAP+sizeof(uint32_t)+sizeof(long)*L2PGAP+1;
	char *oob_data = (char*)calloc(len, 1);
	memcpy(oob_data, (char*)lbas, sizeof(KEYT)*L2PGAP);
	memcpy(&oob_data[sizeof(KEYT)*L2PGAP], &mig_count, sizeof(uint32_t));
	memcpy(&oob_data[sizeof(KEYT)*L2PGAP+sizeof(uint32_t)], (char*)time_stamp, sizeof(long)*L2PGAP);
	//printf("validate_ppa: %u\n", oob_data[sizeof(KEYT)*max_idx]);
	page_ftl.bm->set_oob(page_ftl.bm, oob_data, len,ppa);
	//set_migration_count(page_ftl.bm, mig_count, sizeof(KEYT)*max_idx, ppa);
	free(oob_data);
}


char *get_lbas(struct blockmanager* bm, char* oob_data, int len) {
	//printf("oob_data: 0x%x\n", oob_data);
	char *res = (char *)malloc(len);
	memcpy(res, oob_data, len);
	//KEYT* res_k=(KEYT*)res;
	return res;
}

uint32_t get_migration_count(struct blockmanager* bm, char* oob_data, int len) {
	uint32_t res = 0;
	memcpy(&res, &oob_data[len], sizeof(uint32_t));
	//printf("get_count: %u\n", res);
	return res;	
}

char* get_time_stamp(struct blockmanager* bm, char* oob_data, int len) {
	char *res=(char*)malloc(sizeof(long)*L2PGAP);
	int len2 = len+sizeof(uint32_t);
	memcpy(res, &oob_data[len2], sizeof(unsigned long)*L2PGAP);
	return res;
}

gc_value* send_req(uint32_t ppa, uint8_t type, value_set *value){
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	my_req->parents=NULL;
	my_req->end_req=page_gc_end_req;//call back function for GC
	my_req->type=type;
	
	/*for gc, you should assign free space for reading valid data*/
	gc_value *res=NULL;
	switch(type){
		case GCDR:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->isdone=false;
			res->ppa=ppa;
			my_req->param=(void *)res;
			my_req->type_lower=0;
			/*when read a value, you can assign free value by this function*/
			res->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			page_ftl.li->read(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
		case GCDW:
			res=(gc_value*)malloc(sizeof(gc_value));
			res->value=value;
			my_req->param=(void *)res;
			page_ftl.li->write(ppa,PAGESIZE,res->value,ASYNC,my_req);
			break;
	}
	return res;
}

void new_do_gc(){
	/*this function return a block which have the most number of invalidated page*/
	__gsegment *target=page_ftl.bm->get_gc_target(page_ftl.bm);
	uint32_t page;
	uint32_t bidx, pidx;
	blockmanager *bm=page_ftl.bm;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	//list *temp_list=list_init();
	gc_value **gv_array=(gc_value**)malloc(sizeof(gc_value*)*_PPS);
	align_gc_buffer g_buffer;
	gc_value *gv;
	uint32_t gv_idx=0;

	/*by using this for loop, you can traversal all page in block*/
	for_each_page_in_seg(target,page,bidx,pidx){
		//this function check the page is valid or not
		gv=send_req(page,GCDR,NULL);
		gv_array[gv_idx++]=gv;
	}

	g_buffer.idx=0;
	char *oobs;
	KEYT *lbas;
	uint32_t mig_count=0;

	gv_idx=0;
	uint32_t done_cnt=0;
	unsigned long tmp[4] = {0, 0, 0, 0};
	while(done_cnt!=_PPS){
		gv=gv_array[gv_idx];
		if(!gv->isdone){
			goto next;
		}
		oobs=bm->get_oob(bm, gv->ppa);
		lbas=(KEYT*)get_lbas(bm, oobs, sizeof(KEYT)*L2PGAP);
		mig_count=get_migration_count(bm, oobs, sizeof(KEYT)*L2PGAP);
		
		for(uint32_t i=0; i<L2PGAP; i++){
			if(page_map_pick(lbas[i])!=gv->ppa*L2PGAP+i) continue;
			memcpy(&g_buffer.value[g_buffer.idx*LPAGESIZE],&gv->value->value[i*LPAGESIZE],LPAGESIZE);
			g_buffer.key[g_buffer.idx]=lbas[i];

			g_buffer.idx++;

			if(g_buffer.idx==L2PGAP){
				uint32_t res=page_map_gc_update(g_buffer.key, L2PGAP, 0);
				validate_ppa(res, g_buffer.key, g_buffer.idx, 0, tmp);
				send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
				g_buffer.idx=0;
			}
		}

		done_cnt++;

		inf_free_valueset(gv->value, FS_MALLOC_R);
		free(gv);
next:
		gv_idx=(gv_idx+1)%_PPS;
	}	

	if(g_buffer.idx!=0){
		uint32_t res=page_map_gc_update(g_buffer.key, g_buffer.idx, 0);
		validate_ppa(res, g_buffer.key, g_buffer.idx, 0, tmp);
		send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
		g_buffer.idx=0;	
	}

	bm->trim_segment(bm,target,page_ftl.li); //erase a block

	bm->free_segment(bm, p->active[0]);

	//p->active=p->reserve;//make reserved to active block
	//p->reserve=bm->change_reserve(bm,p->reserve); //get new reserve block from block_manager
}

int calculating_segment_age(__gsegment* target) {
	pm_body *p = (pm_body*)page_ftl.algo_body;
	p->tmp_avg += (req_num - p->seg_timestamp[target->seg_idx]);
        p->gc_seg++;
        if (p->gc_seg == 16) {
                unsigned long new_age = p->tmp_avg/p->gc_seg;
                //printf("new_age: %lu, queuesize: %d\n", new_age, p->q_lbas->m_size);
		p->tmp_avg=0;
                p->gc_seg=0;
                if (p->avg_segage > new_age) {
                        //need to size down
			//printf("=> Q SIZE DOWN\n");
                        q_size_down((int)new_age);
                } else if (p->avg_segage < new_age) {
                        //need to size up
			//printf("=> Q SIZE UP\n");
                        p->q_lbas->m_size = new_age;
                }
                p->avg_segage = new_age;
        }
	return 0;
}

void do_gc(){
	/*this function return a block which have the most number of invalidated page*/
	__gsegment *target=page_ftl.bm->get_gc_target(page_ftl.bm);
	int debug_gnum_info = debug_gnum[target->seg_idx];
	uint32_t page;
	uint32_t bidx, pidx;
	blockmanager *bm=page_ftl.bm;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	list *temp_list=list_init();
	align_gc_buffer g_buffer[4];
	gc_value *gv;

	//check segment age
	calculating_segment_age(target);	

	li_node *now,*nxt;
	//static int cnt=0;
	//printf("gc: %d\n", cnt++);
	/*by using this for loop, you can traversal all page in block*/
	for_each_page_in_seg(target,page,bidx,pidx){
		//this function check the page is valid or not
		bool should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm,page*L2PGAP+i)) continue;
			else{
				should_read=true;
				break;
			}
		}
		if(should_read){
			gv=send_req(page,GCDR,NULL);
			list_insert(temp_list,(void*)gv);
		}
	}

	for (int i=0;i<4;i++) g_buffer[i].idx=0;
	char* oobs;
	KEYT *lbas;
	uint32_t mig_count;
	uint32_t tmp_mig_count = debug_gnum_info;
	int debug_migcount=-1;
	unsigned long *t_timestamp;
	unsigned long calc_age;
	uint32_t page_num=0;

	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){

			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
			oobs=bm->get_oob(bm, gv->ppa);
			lbas=(KEYT*)get_lbas(bm, oobs, sizeof(KEYT)*L2PGAP);
			tmp_mig_count = get_migration_count(bm, oobs, sizeof(KEYT)*L2PGAP);
			if (debug_migcount == -1) debug_migcount = tmp_mig_count;
			else if (debug_migcount != tmp_mig_count) {
				printf("oob has wrong migration count info: do_gc\n");
				abort();
			}
			if (debug_gnum_info != tmp_mig_count) {
				printf("oob info wrong in do_gc\n");
				abort();
			}
			
			t_timestamp = (unsigned long *)get_time_stamp(bm, oobs, sizeof(KEYT)*L2PGAP);
			if (tmp_mig_count == 0) mig_count=0;
			else {
				mig_count=1;
			}
			for(uint32_t i=0; i<L2PGAP; i++){
				if(bm->is_invalid_page(bm,gv->ppa*L2PGAP+i)) continue;
				
				++page_num;
				if (mig_count) {
					calc_age = req_num-t_timestamp[i];
					if (req_num < t_timestamp[i]) {
						printf("timestamp problem in do_gc\n");
						abort();
					}
					if (calc_age < p->avg_segage*4) mig_count=1;
					else if (calc_age < p->avg_segage*16) mig_count=2;
					else mig_count=3;
				}
				memcpy(&g_buffer[mig_count].value[g_buffer[mig_count].idx*LPAGESIZE],&gv->value->value[i*LPAGESIZE],LPAGESIZE);
				g_buffer[mig_count].key[g_buffer[mig_count].idx]=lbas[i];
				g_buffer[mig_count].timestamp[g_buffer[mig_count].idx]=t_timestamp[i];
				g_buffer[mig_count].idx++;
				
				 
				if(g_buffer[mig_count].idx==L2PGAP){
					//if (g_buffer.mig_count >= GNUMBER) printf("problem 1: %d\n", g_buffer.mig_count);
					uint32_t res=page_map_gc_update(g_buffer[mig_count].key, L2PGAP, mig_count);
					validate_ppa(res, g_buffer[mig_count].key, g_buffer[mig_count].idx, mig_count+2, g_buffer[mig_count].timestamp);
					send_req(res, GCDW, inf_get_valueset(g_buffer[mig_count].value, FS_MALLOC_W, PAGESIZE));
					g_buffer[mig_count].idx=0;
				}
			}

			inf_free_valueset(gv->value, FS_MALLOC_R);
			free(gv);
			free(lbas);
			free(t_timestamp);
			//you can get lba from OOB(spare area) in physicall page
			list_delete_node(temp_list,now);
		}
	}
	seg_ratio[tmp_mig_count]--;
	for (int i=0;i<4;i++) {
		if(g_buffer[i].idx!=0){
			//if (g_buffer.mig_count >= GNUMBER) printf("problem 2: %d\n", g_buffer.mig_count);
			uint32_t res=page_map_gc_update(g_buffer[i].key, g_buffer[i].idx, i);
			validate_ppa(res, g_buffer[i].key, g_buffer[i].idx, i+2, g_buffer[i].timestamp);
			send_req(res, GCDW, inf_get_valueset(g_buffer[i].value, FS_MALLOC_W, PAGESIZE));
			g_buffer[i].idx=0;	
		}
	}
	p->stat->gsize[tmp_mig_count]--;
	p->stat->vr[tmp_mig_count] += (double)page_num/(double)(_PPS*L2PGAP);
	p->stat->erase[tmp_mig_count]++;

	bm->trim_segment(bm,target,page_ftl.li); //erase a block
	
	list_free(temp_list);
}


ppa_t get_ppa(KEYT *lbas, uint32_t max_idx, int hc_stat, unsigned long* timestamp){
	uint32_t res;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	/*you can check if the gc is needed or not, using this condition*/
	
	if (page_ftl.bm->get_free_segment_number(page_ftl.bm)<=5) {
		//printf("# of Free Blocks: %d\n",page_ftl.bm->get_free_segment_number(page_ftl.bm)); 
		do_gc();//call gc
	}

retry:
	/*get a page by bm->get_page_num, when the active block doesn't have block, return UINT_MAX*/
	
	
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->active[hc_stat]);

	if(res==UINT32_MAX){
		/*
		 * print normal gc
		if (page_ftl.bm->get_free_segment_number(page_ftl.bm)<=2) {
			++gc_norm_count;
			printf("num of gc: %d\n", gc_norm_count);
		}
		*/
		//printf("free segment: %u\n", page_ftl.bm->get_free_segment_number(page_ftl.bm));
		++seg_ratio[hc_stat];
		p->stat->gsize[hc_stat]++;
		//print_stat();
		__segment *tmp = p->active[hc_stat];
		p->active[hc_stat] = page_ftl.bm->jy_change_reserve(page_ftl.bm, p->active[hc_stat], cur_timestamp);
		page_ftl.bm->free_segment(page_ftl.bm,tmp); //get a new block
		p->seg_timestamp[p->active[hc_stat]->seg_idx] = req_num;
		debug_gnum[p->active[hc_stat]->seg_idx]=hc_stat;
		goto retry;
	}

	/*validate a page*/
	validate_ppa(res, lbas, max_idx, hc_stat, timestamp);
	//printf("assigned %u\n",res);
	return res;
}

void *page_gc_end_req(algo_req *input){
	gc_value *gv=(gc_value*)input->param;
	switch(input->type){
		case GCDR:
			gv->isdone=true;
			break;
		case GCDW:
			/*free value which is assigned by inf_get_valueset*/
			inf_free_valueset(gv->value,FS_MALLOC_R);
			free(gv);
			break;
	}
	free(input);
	return NULL;
}
