#include "page.h"
#include "compaction.h"
#include "lsmtree.h"
#include "skiplist.h"
#include "nocpy.h"
#include "variable.h"
#include "../../include/utils/rwlock.h"
#include "../../include/utils/kvssd.h"
#include "../../interface/interface.h"
#include "../../include/data_struct/list.h"
#include "level.h"
#include "lsmtree_scheduling.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
extern algorithm algo_lsm;
extern lsmtree LSM;
extern volatile int gc_target_get_cnt;
extern volatile int gc_read_wait;
extern pm d_m;
extern pm map_m;
list *gc_hlist;
typedef struct temp_gc_h{
	run_t *d;
	char *data;
}temp_gc_h;

#ifdef KVSSD
uint32_t gc_cnt=0;
int gc_header(){
	//printf("gc_header %u\n",gc_cnt++);
	//printf("gc_header %u",gc_cnt++);
	LSM.header_gc_cnt++;
	gc_general_wait_init();
	lsm_io_sched_flush();

	blockmanager *bm=LSM.bm;
	__gsegment *tseg=bm->pt_get_gc_target(bm,MAP_S);
	//printf("gh : %d\n",tseg->blocks[0]->block_num);
	if(!tseg){
		printf("error invalid gsegment!\n");
		abort();
	}

	//printf("inv number:%d\n",tseg->invalidate_number);
	if(tseg->invalidate_number==0){
		LSM.lop->print_level_summary();
		printf("device full!\n");
		abort();
	}
	if(tseg->blocks[0]->block_num==map_m.active->blocks[0]->block_num){
		free(map_m.active);
	//	printf("tseg, reserve to active\n");
		map_m.active=map_m.reserve;
		map_m.reserve=NULL;
	}

	htable_t **tables=(htable_t**)malloc(sizeof(htable_t*)*_PPS);

	uint32_t tpage=0;
	int bidx=0;
	int pidx=0;
	int i=0;
	
	for_each_page_in_seg(tseg,tpage,bidx,pidx){
		if(bm->is_invalid_page(bm, tpage)){
			continue;
		}
		
		tables[i]=(htable_t*)malloc(sizeof(htable_t));
		gc_data_read(tpage,tables[i],GCMR,NULL);
		i++;
	}
	gc_general_waiting();

	i=0;
	uint32_t invalidate_cnt=0;
	for_each_page_in_seg(tseg,tpage,bidx,pidx){
		if(bm->is_invalid_page(bm, tpage)){
			invalidate_cnt++;
			continue;
		}
		
		KEYT *lpa=LSM.nocpy?LSM.lop->get_lpa_from_data((char*)tables[i]->nocpy_table,tpage,true):LSM.lop->get_lpa_from_data((char*)tables[i]->sets,tpage,true);

		run_t *entries=NULL;
		run_t *target_entry=NULL;
		bool checkdone=false;
		bool shouldwrite=false;
		
		for(int j=0; j<LSM.LEVELN; j++){
			entries=LSM.lop->find_run(LSM.disk[j],*lpa);
			if(entries==NULL) continue;
			if(entries->pbn==tpage && KEYTEST(entries->key,*lpa)){
				if(entries->iscompactioning==SEQMOV) break;
				if(entries->iscompactioning==SEQCOMP) break;

				checkdone=true;
				if(entries->iscompactioning){
					entries->iscompactioning=INVBYGC;
					break;
				}
				target_entry=entries;
				shouldwrite=true;
				break;
			}
			if(checkdone) break;
		}

		if(!checkdone && LSM.c_level){
			entries=LSM.lop->find_run(LSM.c_level,*lpa);
			if(entries){
				if(entries->pbn==tpage){
					checkdone=true;
					shouldwrite=true;
					target_entry=entries;
				}
			}
		}


		if(checkdone==false){
			KEYT temp=*lpa;
			LSM.lop->all_print();
			if(LSM.c_level){
				LSM.lop->print(LSM.c_level);
			}
			printf("[%.*s : %u]error!\n",KEYFORMAT(temp),tpage);
			abort();
		}



		if(!shouldwrite){
			free(tables[i]);
			free(lpa->key);
			free(lpa);
			i++;
			continue;
		}

		uint32_t n_ppa=getRPPA(HEADER,*lpa,true,tseg);
		target_entry->pbn=n_ppa;
		if(LSM.nocpy)nocpy_force_freepage(tpage);
		gc_data_write(n_ppa,tables[i],GCMW);
		free(tables[i]);
		free(lpa->key);
		free(lpa);
		i++;
		continue;
	}

	free(tables);
	for_each_page_in_seg(tseg,tpage,bidx,pidx){
		if(LSM.nocpy) nocpy_trim_delay_enq(tpage);
		if(pidx==0){
			lb_free((lsm_block*)tseg->blocks[bidx]->private_data);
		}
	}
	int res=0;
	bm->pt_trim_segment(bm,MAP_S,tseg,LSM.li);
	if(tseg->blocks[0]->block_num == map_m.active->blocks[0]->block_num){
		res=1;
	}
	free(tseg);
	return res;
}

extern bool target_ppa_invalidate;
gc_node *gc_data_write_new_page(uint32_t t_ppa, char *data, htable_t *table, uint32_t piece, KEYT *lpa){
	LSM.data_gc_cnt++;
	gc_node *res=(gc_node *)malloc(sizeof(gc_node));
	uint32_t n_ppa;
	res->plength=piece;
	kvssd_cpy_key(&res->lpa,lpa);
	
	if(piece==NPCINPAGE){
		res->value=(PTR)table;
	}else{
		PTR t_value=(PTR)malloc(PIECE*piece);
		memcpy(t_value,data,piece*PIECE);
		res->value=t_value;
		n_ppa=-1;
	}
	res->status=NOTISSUE;
	res->invalidate=false;
	res->nppa=n_ppa;
	res->ppa=t_ppa;
	return res;
}
int __gc_data();
int gc_data(){
	LSM.data_gc_cnt++;
	if(LSM.gc_opt){
	//	LSM.lop->print_level_summary();
		while((LSM.LEVELN==1) || LSM.needed_valid_page > (uint32_t) LSM.bm->pt_remain_page(LSM.bm,d_m.active,DATA_S)){
			__gc_data();
			
			if(LSM.LEVELN==1) break;
		}
		uint32_t remain_page=LSM.bm->pt_remain_page(LSM.bm,d_m.active,DATA_S)-LSM.needed_valid_page;
		uint32_t cvt_header=(remain_page*NPCINPAGE)/(LSM.keynum_in_header*LSM.avg_of_length);
		printf("remain page:%d, needed:%d\n",remain_page,LSM.needed_valid_page);
		printf(" convert to header:%d\n",cvt_header);
	//	LSM.added_header=cvt_header;
			
		if(LSM.bm->check_full(LSM.bm,d_m.active,MASTER_BLOCK))
			change_reserve_to_active(DATA);
	}
	else{
		__gc_data();
	}
	return 1;
}
int __gc_data(){
	static int gc_d_cnt=0;
	printf("%d gc_data!\n",gc_d_cnt++);
	/*
	if(LSM.LEVELN!=1){
		compaction_force();
	}*/
	l_bucket *bucket=(l_bucket*)calloc(sizeof(l_bucket),1);
	gc_general_wait_init();

	htable_t **tables=(htable_t**)calloc(sizeof(htable_t*),_PPS);

	blockmanager *bm=LSM.bm;
	__gsegment *tseg=bm->pt_get_gc_target(bm,DATA_S);
	__block *tblock=NULL;
	int tpage=0;
	int bidx=0;
	int pidx=0;
	int i=0;
	//printf("invalidate number:%d\n",tseg->invalidate_number);
	for_each_page_in_seg_blocks(tseg,tblock,tpage,bidx,pidx){
#ifdef DVALUE
		bool page_read=false;
		for(int j=0; j<NPCINPAGE; j++){
			uint32_t npc=tpage*NPCINPAGE+j;
			if(is_invalid_piece((lsm_block*)tblock->private_data,npc)){
				continue;
			}
			else{
				page_read=true;
				tables[i]=(htable_t*)malloc(sizeof(htable_t));
				gc_data_read(npc,tables[i],GCDR,NULL);
				break;
			}
		}
		if(!page_read) continue;
#else
		tables[i]=(htable_t*)malloc(sizeof(htable_t));
		gc_data_read(tpage,tables[i],GCDR);
#endif
		i++;
	}

	gc_general_waiting(); //wait for read req
	
	i=0;
	//int cnt=0;
	for_each_page_in_seg_blocks(tseg,tblock,tpage,bidx,pidx){
		uint32_t t_ppa;
		KEYT *lpa=NULL;
		uint8_t oob_len;
		gc_node *temp_g;
		bool full_page=false;
#ifdef DVALUE
		bool used_page=false;
		footer *foot=(footer*)bm->get_oob(bm,tpage);
		for(int j=0;j<NPCINPAGE; j++){
			t_ppa=tpage*NPCINPAGE+j;

			if(is_invalid_piece((lsm_block*)tblock->private_data,t_ppa)){
				continue;
			}
			used_page=true;
			oob_len=foot->map[j];
			lpa=LSM.lop->get_lpa_from_data(&((char*)tables[i]->sets)[PIECE*j],t_ppa,false);
	#ifdef EMULATOR
			lsm_simul_del(t_ppa);
	#endif
			if(!lpa->len || !oob_len){
				printf("%u oob_len:%u\n",t_ppa,oob_len);
				abort();
			}

			if(oob_len==NPCINPAGE && oob_len%NPCINPAGE==0){
				temp_g=gc_data_write_new_page(t_ppa,NULL,tables[i],NPCINPAGE,lpa);
				full_page=true;
				goto make_bucket;
			}
			else{
				temp_g=gc_data_write_new_page(t_ppa,&((char*)tables[i]->sets)[PIECE*j],NULL,oob_len,lpa);
				if(!bucket->gc_bucket[temp_g->plength])
					bucket->gc_bucket[temp_g->plength]=(gc_node**)malloc(sizeof(gc_node*)*16*_PPS);
				
				bucket->gc_bucket[temp_g->plength][bucket->idx[temp_g->plength]++]=temp_g;
				bucket->contents_num++;
				j+=foot->map[j]-1;
			}
		}
		if(used_page)
			goto next_page;
		else{
			free(lpa);
			continue;
		}
#else 
		t_ppa=tpage;
		lpa=LSM.lop->get_lpa_from_data((char*)tables[i]->sets,t_ppa,false);
		temp_g=gc_data_write_new_page(t_ppa,NULL,tables[i],NPCINPAGE,lpa);
#endif
make_bucket:
		if(!bucket->gc_bucket[temp_g->plength])
			bucket->gc_bucket[temp_g->plength]=(gc_node**)malloc(sizeof(gc_node*)*_PPS);
		bucket->gc_bucket[temp_g->plength][bucket->idx[temp_g->plength]++]=temp_g;
		bucket->contents_num++;
next_page:
		free(lpa);
		if(!full_page) free(tables[i]);
		i++;
	}
	
	free(tables);
	gc_data_header_update_add(bucket);
	for_each_block(tseg,tblock,bidx){
		lb_free((lsm_block*)tblock->private_data);
		tblock->private_data=NULL;
	}

	bm->pt_trim_segment(bm,DATA_S,tseg,LSM.li);
	if(!LSM.gc_opt)
		change_reserve_to_active(DATA);

	for(int i=0; i<NPCINPAGE+1; i++){
		free(bucket->gc_bucket[i]);
	}
	free(bucket);
	free(tseg);
	return 1;
}

int gc_node_compare(const void *a, const void *b){
	gc_node** v1_p=(gc_node**)a;
	gc_node** v2_p=(gc_node**)b;

	gc_node *v1=*v1_p;
	gc_node *v2=*v2_p;

#ifdef KVSSD
	return KEYCMP(v1->lpa,v2->lpa);
#else
	if(v1->lpa>v2->lpa) return 1;
	else if(v1->lpa == v2->lpa) return 0;
	else return -1;
#endif
}

void gc_data_header_update_add(l_bucket *b){
	gc_node **gc_array=(gc_node**)calloc(sizeof(gc_node),b->contents_num);
	int idx=0;
	for(int i=0; i<NPCINPAGE+1; i++){
		for(int j=0; j<b->idx[i]; j++){
			gc_array[idx++]=b->gc_bucket[i][j];
		}
	}

	qsort(gc_array,idx, sizeof(gc_node**),gc_node_compare);
	
	gc_data_header_update(gc_array,idx,b);
	free(gc_array);
}

void* gc_data_end_req(struct algo_req*const req){
	lsm_params *params=(lsm_params*)req->params;
	gc_node *g_target=(gc_node*)params->entry_ptr;

	if(!LSM.nocpy){
		char *target;
		target=(PTR)params->target;
		memcpy(target,params->value->value,PAGESIZE);
	}
	inf_free_valueset(params->value,FS_MALLOC_R);
	g_target->status=READDONE;
	free(params);
	free(req);

	return NULL;
}

uint8_t gc_data_issue_header(struct gc_node *g, gc_params *params, int req_size){
	uint8_t result=0;
	run_t *now=NULL;
	keyset *found=NULL;
retry:
	result=lsm_find_run(g->lpa,&now,&found,&params->level,&params->run);
	switch(result){
		case CACHING:
			if(found){
				if(found->ppa==g->ppa){
					params->found=found;
					g->status=DONE;
				}
				else{
					g->invalidate=true;
					g->plength=0;
					params->level++;
					goto retry;
				}
				return CACHING;
			}
		case FOUND:
			if(now->isflying==1){
				g->status=SAMERUN;
				if(now->gc_wait_idx>req_size){
					printf("over_qdepth!\n");
				}
				now->gc_waitreq[now->gc_wait_idx++]=(void*)g;
				params->data=NULL;
			}
			else{
				g->status=ISSUE;
				if(!now->run_data){
					temp_gc_h *gch=(temp_gc_h*)malloc(sizeof(temp_gc_h));
					params->data=(htable_t*)malloc(sizeof(htable_t));
					now->isflying=1;
					now->gc_waitreq=(void**)calloc(sizeof(void*),req_size);
					now->gc_wait_idx=0;
					gch->d=now;
					gch->data=(char*)params->data;
					list_insert(gc_hlist,(void*)gch);
					gc_data_read(now->pbn,params->data,GCMR_DGC,g);
				}
				else{
					params->data=NULL;
					g->status=READDONE;
					now->isflying=1;
					memset(now->gc_waitreq,0,sizeof(void*)*req_size);
					now->gc_wait_idx=0;
				}
			}
			break;
		case NOTFOUND:
			result=lsm_find_run(g->lpa,&now,&found,&params->level,&params->run);
			LSM.lop->all_print();
			printf("lpa: %.*s ppa:%u\n",KEYFORMAT(g->lpa),g->ppa);
			abort();
			break;
	}
	params->ent=now;
	return FOUND;
}

uint32_t gc_data_each_header_check(struct gc_node *g, int size){
	gc_params *_p=(gc_params*)g->params;
	int done_cnt=0;
	keyset *find;
	run_t *ent=_p->ent;
	htable_t *data=_p->data?_p->data:(htable_t *)ent->run_data;
	if(!data){
		printf("run_data:%p\n",ent->from_req);
	}
	ent->run_data=(void*)data;
	if(!data){
		printf("lpa: %.*s ppa:%u \n",KEYFORMAT(g->lpa),g->ppa);
		abort();
	}
	/*
	static int cnt=0;
	//printf("test:%d\n",cnt++);
	bool test=false;
	if(42901==cnt++){
		test=true;
	}*/
	/*
		0 - useless -> free
		1 - target is correct
		2 - target is incoreect but it is for other nodes;
	 */
	bool set_flag=false;
	bool original_target_processed=false;
	for(int i=-1; i<ent->gc_wait_idx; i++){
		gc_node *target=i==-1?g:(gc_node*)ent->gc_waitreq[i];
		gc_params *p=(gc_params*)target->params;
		/*
		if(test){
			printf("%.*s - %d\n",KEYFORMAT(target->lpa),i);
			printf("data :%p\n",data);
			printf("data_nocpy:%p\n",data->nocpy_table);
		}*/
		find=LSM.nocpy?LSM.lop->find_keyset((char*)data->nocpy_table,target->lpa): LSM.lop->find_keyset((char*)data->sets,target->lpa);
		if(find && find->ppa==target->ppa){
			p->found=find;
			target->status=DONE;
			done_cnt++;
			
			if(ent->c_entry){
				if(LSM.nocpy){
					p->found2=LSM.lop->find_keyset((char*)ent->cache_nocpy_data_ptr,target->lpa);
				}
				else{
					p->found2=LSM.lop->find_keyset((char*)ent->cache_data->sets,target->lpa);
				}
			}
			else
				p->found2=NULL;
			if(!set_flag && i==-1){
				set_flag=true;
				original_target_processed=true;
			}
			else if(!set_flag){
				set_flag=true;
			}
		}
		else{
			if(find){
				target->invalidate=true;
				target->plength=0;
			}
			
			if(i!=-1){
				target->status=RETRY;
			}
			p->run++;
		}
	}
	
	if(!original_target_processed){
		gc_data_issue_header(g,_p,size);
	}
	ent->isflying=0;
	return done_cnt;
}

void gc_data_header_update(struct gc_node **g, int size, l_bucket *b){
	uint32_t done_cnt=0;
	int result=0;
	gc_params *params;
	int cnttt=0;
	gc_hlist=list_init();

	while(done_cnt!=size){
		cnttt++;
		for(int i=0; i<size; i++){
			if(g[i]->status==DONE) continue;
			gc_node *target=g[i];
			switch(target->status){
				case NOTISSUE:
					params=(gc_params*)malloc(sizeof(gc_params));
					memset(params,0,sizeof(gc_params));
					target->params=(void*)params;
				case RETRY:
					result=gc_data_issue_header(target,(gc_params*)target->params,size);
					//if(result==CACHING) done_cnt++;
					break;
				case READDONE:
					done_cnt+=gc_data_each_header_check(target,size);
					break;
			}
			if(done_cnt>size){
				abort();
			}
		}
	}

	gc_read_wait=0;
	
	int g_idx;
	for(int i=0; i<b->idx[NPCINPAGE]; i++){
		gc_node *t=b->gc_bucket[NPCINPAGE][i];
		if(t->plength==0) continue;
		LSM.lop->moveTo_fr_page(true);
		t->nppa=LSM.lop->get_page(NPCINPAGE,t->lpa);
		footer *foot=(footer*)pm_get_oob(CONVPPA(t->nppa),DATA,false);
		foot->map[0]=NPCINPAGE;
		gc_data_write(t->nppa,(htable_t*)t->value,GCDW);
	}
	b->idx[NPCINPAGE]=0;
	variable_value2Page(NULL,b,NULL,&g_idx,true);
	

	htable_t** map_table=(htable_t**)malloc(sizeof(htable_t*)*size);
	run_t **entries=(run_t**)malloc(sizeof(run_t*)*size);
	int idx=0;
	for(int i=0;i<size; i++){
		gc_node *t=g[i];
		gc_params *p=(gc_params*)t->params;
		if(!p->found) 
			abort();
		else{
			p->found->ppa=t->plength==0?-1:t->nppa;	
			if(p->found2) p->found2->ppa=p->found->ppa;
		}
		if(p->data){
			map_table[idx]=p->data;
			entries[idx]=p->ent;
			idx++;
		}
		free(t->lpa.key);
		free(t->value);
		free(t);
		free(p);
	}


	char *nocpy_temp_table;
	for(int i=0; i<idx; i++){
		ppa_t temp_header=entries[i]->pbn;
		entries[i]->run_data=NULL;
		if(LSM.nocpy){
			nocpy_temp_table=map_table[i]->nocpy_table;
			nocpy_force_freepage(entries[i]->pbn);
		}
		invalidate_PPA(HEADER,temp_header);
		entries[i]->pbn=getPPA(HEADER,entries[i]->key,true);
		if(LSM.nocpy) {map_table[i]->nocpy_table=nocpy_temp_table;}
		gc_data_write(entries[i]->pbn,map_table[i],GCMW_DGC);
	}
	free(entries);
	free(map_table);

	li_node *ln, *lp;
	if(gc_hlist->size!=0){
		for_each_list_node_safe(gc_hlist,ln,lp){
			temp_gc_h *gch=(temp_gc_h*)ln->data;
			free(gch->data);
			gch->d->run_data=NULL;
			free(gch->d->gc_waitreq);
			free(gch);
		}
	}
	list_free(gc_hlist);
}	
#endif
