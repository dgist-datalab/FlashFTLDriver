#include "pipe.h"
#include "array.h"
#include "../../lsmtree.h"
#include <stdio.h>
#include <stdlib.h>
#include "../../../../bench/bench.h"
extern MeasureTime write_opt_time[10];
extern lsmtree LSM;
p_body *pbody_init(char **data,uint32_t size, pl_run *pl_datas, bool read_from_run,BF *filter){
	p_body *res=(p_body*)calloc(sizeof(p_body),1);
	res->data_ptr=data;
	res->max_page=size;
	res->read_from_run=read_from_run;
	res->pl_datas=pl_datas;
#ifdef BLOOM
	if(filter){
		res->filter=filter;
	}
#endif
	return res;
}

bool print_test;
int lock_cnt=0;
void new_page_set(p_body *p, bool iswrite){
	if(p->read_from_run){
		if(fdriver_try_lock(p->pl_datas[p->pidx].lock)==-1){
			fdriver_lock(p->pl_datas[p->pidx].lock);
		}
		else{
		}
		p->now_page=data_from_run(p->pl_datas[p->pidx].r);
	}
	else{
		if(p->pidx>=p->max_page){
			printf("%d %d \n", p->pidx, p->max_page);
		}
		p->now_page=p->data_ptr[p->pidx];

	}
	if(iswrite && !p->now_page){
		p->now_page=(char*)malloc(PAGESIZE);
	}
	p->bitmap_ptr=(uint16_t *)p->now_page;
	p->kidx=1;
	p->max_key=p->bitmap_ptr[0];
	p->length=1024;
	p->pidx++;
}

KEYT pbody_get_next_key(p_body *p, uint32_t *ppa){
	if(!p->now_page || (p->pidx<p->max_page && p->kidx>p->max_key)){
		new_page_set(p,false);
	}

	KEYT res={0,};
	if(p->pidx>=p->max_page && p->kidx>p->max_key){
		res.len=-1;
		return res;
	}

	memcpy(ppa,&p->now_page[p->bitmap_ptr[p->kidx]],sizeof(uint32_t));
	res.len=p->bitmap_ptr[p->kidx+1]-p->bitmap_ptr[p->kidx]-sizeof(uint32_t);
	res.key=&p->now_page[p->bitmap_ptr[p->kidx]+sizeof(uint32_t)];
	p->kidx++;
	return res;
}

bool test_flag;
char *pbody_insert_new_key(p_body *p,KEYT key, uint32_t ppa, bool flush)
{
	char *res=NULL;
	static int key_cnt=0;
	if((flush && p->kidx>1) || !p->now_page || p->kidx>=(PAGESIZE-1024)/sizeof(uint16_t)-2 || p->length+(key.len+sizeof(uint32_t))>PAGESIZE){
		if(p->now_page){
			p->bitmap_ptr[0]=p->kidx-1;
			p->bitmap_ptr[p->kidx]=p->length;
			p->data_ptr[p->pidx-1]=p->now_page;
			res=p->now_page;
		}
		if(flush){
			return res;
		}
		new_page_set(p,true);
		key_cnt=0;
	}

	char *target_idx=&p->now_page[p->length];
	memcpy(target_idx,&ppa,sizeof(uint32_t));
	memcpy(&target_idx[sizeof(uint32_t)],key.key,key.len);

#ifdef BLOOM
	if(p->filter)
		bf_set(p->filter,key);
#endif

	p->bitmap_ptr[p->kidx++]=p->length;
	p->length+=sizeof(uint32_t)+key.len;
	return res;
}


char *pbody_get_data(p_body *p, bool init)
{
	if(init){
		p->max_page=p->pidx;
		p->pidx=0;
	}

	if(p->pidx<p->max_page){
		return p->data_ptr[p->pidx++];
	}
	else{
		return NULL;
	}
}

char *pbody_clear(p_body *p){
	free(p);
	return NULL;
}
