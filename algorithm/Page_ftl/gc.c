#include "gc.h"
#include "map.h"
#include "../../include/data_struct/list.h"
#include <stdlib.h>
#include <stdint.h>

extern uint32_t *seg_ratio;
uint32_t gc_norm_count=0;
extern FILE *vFile;
//extern FILE *gFile;
//extern FILE *wFile;

extern FILE *hFile_10;
extern FILE *hFile_20;
extern FILE *hFile_30;
extern FILE *hFile_40;
extern FILE *hFile_50;

extern FILE *tFile_10;
extern FILE *tFile_20;
extern FILE *tFile_30;
extern FILE *tFile_40;
extern FILE *tFile_50;



extern uint32_t req_num;

extern algorithm page_ftl;
void invalidate_ppa(uint32_t t_ppa){
	/*when the ppa is invalidated this function must be called*/
	page_ftl.bm->unpopulate_bit(page_ftl.bm, t_ppa);
}

void validate_ppa(uint32_t ppa, KEYT *lbas, uint32_t max_idx, uint32_t mig_count){
	/*when the ppa is validated this function must be called*/
	for(uint32_t i=0; i<max_idx; i++){
		page_ftl.bm->populate_bit(page_ftl.bm,ppa * L2PGAP+i);
	}

	/*this function is used for write some data to OOB(spare area) for reverse mapping*/
	int len=sizeof(KEYT)*max_idx+sizeof(uint32_t)+1;
	char *oob_data = (char*)calloc(len, 1);
	memcpy(oob_data, (char*)lbas, sizeof(KEYT)*max_idx);
	memcpy(&oob_data[sizeof(KEYT)*max_idx], &mig_count, sizeof(uint32_t));
	//printf("validate_ppa: %u\n", oob_data[sizeof(KEYT)*max_idx]);
	page_ftl.bm->set_oob(page_ftl.bm, oob_data, len,ppa);
	//set_migration_count(page_ftl.bm, mig_count, sizeof(KEYT)*max_idx, ppa);
	free(oob_data);
}


char *get_lbas(struct blockmanager* bm, char* oob_data, int len) {
	//printf("oob_data: 0x%x\n", oob_data);
	char *res = (char *)malloc(len);
	memcpy(res, oob_data, len);
	KEYT* res_k=(KEYT*)res;
	return res;
}

uint32_t get_migration_count(struct blockmanager* bm, char* oob_data, int len) {
	uint32_t res = 0;
	memcpy(&res, &oob_data[len], sizeof(uint32_t));
	//printf("get_count: %u\n", res);
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
				validate_ppa(res, g_buffer.key, g_buffer.idx, 0);
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
		validate_ppa(res, g_buffer.key, g_buffer.idx, 0);
		send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
		g_buffer.idx=0;	
	}

	bm->trim_segment(bm,target,page_ftl.li); //erase a block

	bm->free_segment(bm, p->active);

	//p->active=p->reserve;//make reserved to active block
	//p->reserve=bm->change_reserve(bm,p->reserve); //get new reserve block from block_manager
}

void do_gc(){
	/*this function return a block which have the most number of invalidated page*/
	__gsegment *target=page_ftl.bm->get_gc_target(page_ftl.bm);
	uint32_t page;
	uint32_t bidx, pidx;
	blockmanager *bm=page_ftl.bm;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	list *temp_list=list_init();
	list *hot_list=list_init();
	align_gc_buffer g_buffer;
	gc_value *gv;

	li_node *now,*nxt;
	uint32_t hot_10=0;
	uint32_t hot_20=0;
	uint32_t hot_30=0;
	uint32_t hot_40=0;
	uint32_t hot_50=0;
	uint32_t tot_num[5]={0,};
	uint32_t list_hot_08[5] = {361, 5828, 34346, 126253, 351780};
	uint32_t list_hot_11[5] = {0, 3, 13, 52, 229};
	uint32_t list_hot_ran[5] = {627457, 1321580, 2056258, 2824995, 3627312};
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
		} else gv = send_req(page,GCDR,NULL);
		list_insert(hot_list, (void*)gv);
		while(hot_list->size) {
			for_each_list_node_safe(hot_list, now, nxt) {
			gv=(gc_value*)now->data;
			if (!gv->isdone)continue;
			char* toob = bm->get_oob(bm, gv->ppa);
			KEYT *tlba = (KEYT*)get_lbas(bm, toob, sizeof(KEYT)*L2PGAP);
			for (uint32_t i=0;i<L2PGAP; i++) {
				if (BENCH_JY == 0) {
						//zipfian 08
						int tt;
						tt=tlba[i];
						if (tt <= list_hot_08[4]) {
							++tot_num[4];
							if (tt <= list_hot_08[3]) {
								++tot_num[3];
								if (tt <= list_hot_08[2]) {
									++tot_num[2];
									if (tt <= list_hot_08[1]) {
										++tot_num[1];
										if (tt <= list_hot_08[0]) {
											++tot_num[0];
										}
									}
								}
							}
						}
					} else if (BENCH_JY == 1) {
						//zipfian 11
						int tt;
						tt=tlba[i];
						if (tt <= list_hot_11[4]) {
							++tot_num[4];
							if (tt <= list_hot_11[3]) {
								++tot_num[3];
								if (tt <= list_hot_11[2]) {
									++tot_num[2];
									if (tt <= list_hot_11[1]) {
										++tot_num[1];
										if (tt <= list_hot_11[0]) {
											++tot_num[0];
										}
									}
								}
							}
						}
				
					} else  {
						//random
						int tt;
						tt=tlba[i];
						if (tt <= list_hot_ran[4]) {
							++tot_num[4];
							if (tt <= list_hot_ran[3]) {
								++tot_num[3];
								if (tt <= list_hot_ran[2]) {
									++tot_num[2];
									if (tt <= list_hot_ran[1]) {
										++tot_num[1];
										if (tt <= list_hot_ran[0]) {
											++tot_num[0];
										}
									}
								}
							}
						}
					}
			}
			if (!should_read) {
				inf_free_valueset(gv->value, FS_MALLOC_R);
				free(gv);
			}
			free(tlba);
			list_delete_node(hot_list,now);
		}}
	}

	g_buffer.idx=0;
	char* oobs;
	KEYT *lbas;
	uint32_t mig_count;
	uint32_t tmp_mig_count;
	const uint32_t tot_page_num=512;
	uint32_t page_num=0;
	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){

			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
			oobs=bm->get_oob(bm, gv->ppa);
			lbas=(KEYT*)get_lbas(bm, oobs, sizeof(KEYT)*L2PGAP);
			mig_count = get_migration_count(bm, oobs, sizeof(KEYT)*L2PGAP);
			tmp_mig_count=mig_count;
			if (mig_count < GNUMBER-1) g_buffer.mig_count=mig_count+1;
			else g_buffer.mig_count=mig_count;

			for(uint32_t i=0; i<L2PGAP; i++){
				if(bm->is_invalid_page(bm,gv->ppa*L2PGAP+i)) continue;
				++page_num;
				memcpy(&g_buffer.value[g_buffer.idx*LPAGESIZE],&gv->value->value[i*LPAGESIZE],LPAGESIZE);
				g_buffer.key[g_buffer.idx]=lbas[i];
				g_buffer.idx++;
				if (BENCH_JY == 0) {
					//zipfian 08
					int tt;
					tt=lbas[i];
					if (tt <= list_hot_08[4]) {
						++hot_50;
						if (tt <= list_hot_08[3]) {
							++hot_40;
							if (tt <= list_hot_08[2]) {
								++hot_30;
								if (tt <= list_hot_08[1]) {
									++hot_20;
									if (tt <= list_hot_08[0]) {
										++hot_10;
									}
								}
							}
						}
					}
				} else if (BENCH_JY == 1) {
					//zipfian 11
					int tt;
					tt=lbas[i];
					if (tt <= list_hot_11[4]) {
						++hot_50;
						if (tt <= list_hot_11[3]) {
							++hot_40;
							if (tt <= list_hot_11[2]) {
								++hot_30;
								if (tt <= list_hot_11[1]) {
									++hot_20;
									if (tt <= list_hot_11[0]) {
										++hot_10;
									}
								}
							}
						}
					}
			
				} else  {
					//random
					int tt;
					tt=lbas[i];
					if (tt <= list_hot_ran[4]) {
						++hot_50;
						if (tt <= list_hot_ran[3]) {
							++hot_40;
							if (tt <= list_hot_ran[2]) {
								++hot_30;
								if (tt <= list_hot_ran[1]) {
									++hot_20;
									if (tt <= list_hot_ran[0]) {
										++hot_10;
									}
								}
							}
						}
					}
				} 
				if(g_buffer.idx==L2PGAP){
					//if (g_buffer.mig_count >= GNUMBER) printf("problem 1: %d\n", g_buffer.mig_count);
					uint32_t res=page_map_gc_update(g_buffer.key, L2PGAP, g_buffer.mig_count);
					validate_ppa(res, g_buffer.key, g_buffer.idx, g_buffer.mig_count);
					send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
					g_buffer.idx=0;
				}
			}

			inf_free_valueset(gv->value, FS_MALLOC_R);
			free(gv);
			free(lbas);
			//you can get lba from OOB(spare area) in physicall page
			list_delete_node(temp_list,now);
		}
	}
	if (tmp_mig_count > 0) seg_ratio[tmp_mig_count-1]--;
	if(g_buffer.idx!=0){
		//if (g_buffer.mig_count >= GNUMBER) printf("problem 2: %d\n", g_buffer.mig_count);
		uint32_t res=page_map_gc_update(g_buffer.key, g_buffer.idx, g_buffer.mig_count);
		validate_ppa(res, g_buffer.key, g_buffer.idx, g_buffer.mig_count);
		send_req(res, GCDW, inf_get_valueset(g_buffer.value, FS_MALLOC_W, PAGESIZE));
		g_buffer.idx=0;	
	}
	bm->trim_segment(bm,target,page_ftl.li); //erase a block
	
	// print valid ratio
	char val_buf[64];
	sprintf(val_buf, "%d %f\n", tmp_mig_count+1, (float)page_num/(float)tot_page_num*(float)100);
	fputs(val_buf, vFile);
	
	//printf("gc occurs, valid page: %f%%\n", (float)page_num/(float)tot_page_num*(float)100);
	
	//print hot ratio
	char hot_buf[64];
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)hot_10/(float)page_num*(float)100);
	fputs(hot_buf, hFile_10);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)hot_20/(float)page_num*(float)100);
	fputs(hot_buf, hFile_20);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)hot_30/(float)page_num*(float)100);
	fputs(hot_buf, hFile_30);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)hot_40/(float)page_num*(float)100);
	fputs(hot_buf, hFile_40);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)hot_50/(float)page_num*(float)100);
	fputs(hot_buf, hFile_50);
	
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)tot_num[0]/(float)tot_page_num*(float)100);
	fputs(hot_buf, tFile_10);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)tot_num[1]/(float)tot_page_num*(float)100);
	fputs(hot_buf, tFile_20);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)tot_num[2]/(float)tot_page_num*(float)100);
	fputs(hot_buf, tFile_30);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)tot_num[3]/(float)tot_page_num*(float)100);
	fputs(hot_buf, tFile_40);
	sprintf(hot_buf, "%d %f\n", tmp_mig_count+1, (float)tot_num[4]/(float)tot_page_num*(float)100);
	fputs(hot_buf, tFile_50);


	// * print # of current segment number
	/*
	for (int i=0;i<(GNUMBER-1);++i) {
		printf("current %d group segment number: %d\n", i+1, seg_ratio[i]);
	}
	*/
	/*
	if (bm->check_full(bm, p->active, MASTER_PAGE)) {
		bm->free_segment(bm, p->active);

		p->active=bm->get_segment(page_ftl.bm, false);//make reserved to active block
	//p->reserve=bm->change_reserve(bm,p->reserve); //get new reserve block from block_manager
	}
	*/
	list_free(hot_list);
	list_free(temp_list);
}


ppa_t get_ppa(KEYT *lbas, uint32_t max_idx){
	uint32_t res;
	pm_body *p=(pm_body*)page_ftl.algo_body;
	/*you can check if the gc is needed or not, using this condition*/
//	if(page_ftl.bm->check_full(page_ftl.bm, p->active,MASTER_PAGE) && (page_ftl.bm->get_free_segment_number(page_ftl.bm)<=5)){
//		new_do_gc();//call gc
	/*
	if ((req_num%8388608==0) || (req_num/4>69206014)) {
		char gnum_buf[64];
		//block number per groups
		for (int i=0;i<(GNUMBER-1);++i) {
			sprintf(gnum_buf, "%d %d\n", i+1, seg_ratio[i]);
			fputs(gnum_buf, gFile);
		}
		//printf("%lu %lu \n", page_ftl.bm->li->req_type_cnt[6], page_ftl.bm->li->req_type_cnt[8]);
		//WAF	
		sprintf(gnum_buf, "%f\n", (float)((float)(page_ftl.bm->li->req_type_cnt[6]+page_ftl.bm->li->req_type_cnt[8])
					/(float)(page_ftl.bm->li->req_type_cnt[6])));
		fputs(gnum_buf, wFile);
		
	}
	*/
	if (page_ftl.bm->get_free_segment_number(page_ftl.bm)<=2) {
		//printf("# of Free Blocks: %d\n",page_ftl.bm->get_free_segment_number(page_ftl.bm)); 
		do_gc();//call gc
	}

retry:
	/*get a page by bm->get_page_num, when the active block doesn't have block, return UINT_MAX*/
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->active);

	if(res==UINT32_MAX){
		/*
		 * print normal gc
		if (page_ftl.bm->get_free_segment_number(page_ftl.bm)<=2) {
			++gc_norm_count;
			printf("num of gc: %d\n", gc_norm_count);
		}
		*/
		//printf("free segment: %u\n", page_ftl.bm->get_free_segment_number(page_ftl.bm));
		page_ftl.bm->free_segment(page_ftl.bm, p->active);
		p->active=page_ftl.bm->get_segment(page_ftl.bm,false); //get a new block
		goto retry;
	}

	/*validate a page*/
	validate_ppa(res, lbas, max_idx, 0);
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
