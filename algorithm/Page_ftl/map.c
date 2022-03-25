#include "map.h"
#include "gc.h"
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

extern uint32_t test_key;
extern algorithm page_ftl;

uint32_t *seg_ratio;
uint32_t req_num=0;
char gcur=0;
FILE *vFile;
//FILE *gFile;
//FILE *wFile;

FILE *hFile_10;
FILE *hFile_20;
FILE *hFile_30;
FILE *hFile_40;
FILE *hFile_50;

FILE *tFile_10;
FILE *tFile_20;
FILE *tFile_30;
FILE *tFile_40;
FILE *tFile_50;



void page_map_create(){
	printf("NOS: %d\n", _NOS);
	seg_ratio=(uint32_t*)calloc((GNUMBER), sizeof(uint32_t));
	pm_body *p=(pm_body*)calloc(sizeof(pm_body),1);
	p->mapping=(uint32_t*)malloc(sizeof(uint32_t)*_NOP*L2PGAP);
	for(int i=0;i<_NOP*L2PGAP; i++){
		p->mapping[i]=UINT_MAX;
	}
	p->reserve=(__segment **)malloc(sizeof(__segment*)*(GNUMBER-1));
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

	char *ttp=(char*)malloc(4);
	if (BENCH_JY == 0) ttp = "08";
	else if (BENCH_JY == 1) ttp = "11";
	else ttp = "ran";	
	sprintf(name, "./valid_ratio/r_%s_hot_10", ttp);
	hFile_10 = fopen(name, "w");
	sprintf(name, "./valid_ratio/r_%s_hot_20", ttp);
	hFile_20 = fopen(name, "w");
	sprintf(name, "./valid_ratio/r_%s_hot_30", ttp);
	hFile_30 = fopen(name, "w");
	sprintf(name, "./valid_ratio/r_%s_hot_40", ttp);
	hFile_40 = fopen(name, "w");
	sprintf(name, "./valid_ratio/r_%s_hot_50", ttp);
	hFile_50 = fopen(name, "w");

	sprintf(name, "./valid_ratio/tot_%s_hot_10", ttp);
	tFile_10 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tot_%s_hot_20", ttp);
	tFile_20 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tot_%s_hot_30", ttp);
	tFile_30 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tot_%s_hot_40", ttp);
	tFile_40 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tot_%s_hot_50", ttp);
	tFile_50 = fopen(name, "w");

	sprintf(name, "./valid_ratio/valid_%s", ttp);
	vFile = fopen(name, "w");


/*
	sprintf(name, "./valid_ratio/tmp_10");
	hFile_10 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tmp_20");
	hFile_20 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tmp_30");
	hFile_30 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tmp_40");
	hFile_40 = fopen(name, "w");
	sprintf(name, "./valid_ratio/tmp_50");
	hFile_50 = fopen(name, "w");
*/

	setbuf(hFile_10, NULL);
	setbuf(hFile_20, NULL);
	setbuf(hFile_30, NULL);
	setbuf(hFile_40, NULL);
	setbuf(hFile_50, NULL);

	setbuf(tFile_10, NULL);
        setbuf(tFile_20, NULL);
        setbuf(tFile_30, NULL);
        setbuf(tFile_40, NULL);
        setbuf(tFile_50, NULL);

	setbuf(vFile, NULL);
	p->active=page_ftl.bm->get_segment(page_ftl.bm,true); //now active block for inserted request.
	page_ftl.algo_body=(void*)p; //you can assign your data structure in algorithm structure
}

uint32_t page_map_assign(KEYT* lba, uint32_t max_idx){
	//printf("lba : %lu\n", lba);
	uint32_t res=0;
	req_num+=4;
	res=get_ppa(lba, L2PGAP);
	pm_body *p=(pm_body*)page_ftl.algo_body;
	for(uint32_t i=0; i<L2PGAP; i++){
		KEYT t_lba=lba[i];
		//printf("key: %lu\n", t_lba);
		uint32_t previous_ppa=p->mapping[t_lba];
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
	uint32_t group_idx = mig_count-1;
	if (mig_count == 0) group_idx = mig_count;
retry:
	if (gcur <= group_idx) {
		//initialize migration group
		p->reserve[group_idx] = page_ftl.bm->get_segment(page_ftl.bm, true);
		++gcur;
	}
	res=page_ftl.bm->get_page_num(page_ftl.bm,p->reserve[group_idx]);
	if (res==UINT32_MAX){
		__segment* tmp=p->reserve[group_idx];
		seg_ratio[group_idx+1]++;
		p->reserve[group_idx] = page_ftl.bm->change_reserve(page_ftl.bm, p->reserve[group_idx]);
		page_ftl.bm->free_segment(page_ftl.bm, tmp);
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
	free(seg_ratio);
	free(p->reserve);
	free(p);
	fclose(vFile);
	//fclose(gFile);
	//fclose(wFile);
	
	fclose(hFile_10);
	fclose(hFile_20);
	fclose(hFile_30);
	fclose(hFile_40);
	fclose(hFile_50);
	
	fclose(tFile_10);
	fclose(tFile_20);
	fclose(tFile_30);
	fclose(tFile_40);
	fclose(tFile_50);
	

}


