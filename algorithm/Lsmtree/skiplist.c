#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<limits.h>
#include<unistd.h>
#include<sys/types.h>
#include"skiplist.h"
#include"../../interface/interface.h"
#include "../../include/utils/kvssd.h"
#include "../../include/slab.h"
#include "../../bench/bench.h"
#ifdef Lsmtree
#include"variable.h"
#include "lsmtree.h"
#include "level.h"
#include "page.h"


extern MeasureTime write_opt_time[10];
extern lsmtree LSM;

#endif

extern KEYT key_max, key_min;

#ifdef USINGSLAB
//extern struct slab_chain snode_slab;
extern kmem_cache_t snode_slab;
#endif
skiplist *skiplist_init(){
	skiplist *point=(skiplist*)malloc(sizeof(skiplist));
	point->level=1;
#ifdef USINGSLAB
	//point->header=(snode*)slab_alloc(&snode_slab);
	point->header=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
	point->header=(snode*)malloc(sizeof(snode));
#endif
	point->header->list=(snode**)malloc(sizeof(snode*)*(MAX_L+1));
	for(int i=0; i<MAX_L; i++) point->header->list[i]=point->header;
	//back;
	point->header->back=point->header;

#if defined(KVSSD)
	point->all_length=0;
	point->header->key=key_max;
#else
	point->header->key=UINT_MAX;
	point->start=UINT_MAX;
	point->end=0;
#endif
	point->header->value=NULL;
	point->size=0;
	return point;
}

snode *skiplist_find(skiplist *list, KEYT key){
	if(!list) return NULL;
	if(list->size==0) return NULL;
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
	}

#if defined(KVSSD)
	if(KEYTEST(x->list[1]->key,key))
#else
	if(x->list[1]->key==key)
#endif
		return x->list[1];
	return NULL;
}

snode *skiplist_find_lowerbound(skiplist *list, KEYT key){
	if(!list) return NULL;
	if(list->size==0) return NULL;
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD)
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
	}

	return x->list[1];
}

snode *skiplist_range_search(skiplist *list,KEYT key){
	if(list->size==0) return NULL;
	snode *x=list->header;
	snode *bf=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD)
		while(KEYCMP(x->list[i]->key,key)<=0)
#else
		while(x->list[i]->key<=key)
#endif
		{
			bf=x;
			x=x->list[i];
		}
	}
	
	bf=x;
	x=x->list[1];
#if defined(KVSSD)
	if(KEYCMP(bf->key,key)<=0 && KEYCMP(key,x->key)<0)
#else
	if(bf->key<=key && key< x->key)
#endif
	{
		return bf;
	}


#if defined(KVSSD)
	if(KEYCMP(key,list->header->list[1]->key)<=0)
#else
	if(key<=list->header->list[1]->key)
#endif
	{
		return list->header->list[1];
	}
	return NULL;
}

snode *skiplist_strict_range_search(skiplist *list,KEYT key){
	if(list->size==0) return NULL;
	snode *x=list->header;
	snode *bf=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) 
		while(KEYCMP(x->list[i]->key,key)<=0)
#else
		while(x->list[i]->key<=key)
#endif	
		{
			bf=x;
			x=x->list[i];
		}
	}
	
	bf=x;
	x=x->list[1];
#if defined(KVSSD) 
	if(KEYCMP(bf->key,key)<=0 && KEYCMP(key,x->key)<0)
#else
	if(bf->key<=key && key< x->key)
#endif
	{
		return bf;
	}

#if defined(KVSSD) 
	else if(KEYCMP(bf->key,key_max)==0)
#else
	else if(bf->key==UINT_MAX)
#endif
	{
		return x;
	}
	return NULL;
}

static int getLevel(){
	int level=1;
	int temp=rand();
	while(temp % PROB==1){
		temp=rand();
		level++;
		if(level+1>=MAX_L) break;
	}
	return level;
}

#ifdef Lsmtree
snode *skiplist_insert_wP(skiplist *list, KEYT key, ppa_t ppa,bool deletef){
#if !(defined(KVSSD) )
	if(key>RANGE){
		printf("bad page read key:%u\n",key);
		return NULL;
	}
#endif
	snode *update[MAX_L+1];
	snode *x=list->header;

	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) 
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	
	x=x->list[1];

#if defined(KVSSD) && defined(Lsmtree)
	if(KEYTEST(key,x->key))
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
		//ignore new one;
		invalidate_PPA(DATA,ppa);
		abort();
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
	//	x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

#ifdef KVSSD
		list->all_length+=KEYLEN(key);
#endif
		x->key=key;
		x->ppa=ppa;
		x->isvalid=deletef;
#ifdef Lsmtree
		x->iscaching_entry=false;
#endif
		x->value=NULL;
		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}

		//new back
		x->back=x->list[1]->back;
		x->list[1]->back=x;

		x->level=level;
		list->size++;
	}
	return x;
}

snode *skiplist_insert_existIgnore(skiplist *list,KEYT key,ppa_t ppa,bool deletef){
#ifndef KVSSD
	if(key>RANGE){
		printf("bad page read\n");
		return NULL;
	}
#endif
	snode *update[MAX_L+1];
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#ifdef KVSSD
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
		{
			x=x->list[i];
		}
		update[i]=x;
	}

	x=x->list[1];
#ifdef KVSSD
	if(KEYTEST(key,x->key))
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
		//delete exists ppa; input ppa
		if(ppa==x->ppa){
			printf("%.*s ppa:%u",KEYFORMAT(key),ppa);
			abort();
		}

		invalidate_PPA(DATA,x->ppa);
	
		x->ppa=ppa;
		x->isvalid=deletef;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
		//x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

#ifdef KVSSD
		list->all_length+=KEYLEN(key);
#endif

#ifdef Lsmtree
		x->iscaching_entry=false;
#endif
		x->key=key;
		x->ppa=ppa;
		x->isvalid=deletef;
		x->value=NULL;
		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}

		//new back
		x->back=x->list[1]->back;
		x->list[1]->back=x;

		x->level=level;
		list->size++;
	}
	return x;
}

snode *skiplist_general_insert(skiplist *list,KEYT key,void* value,void (*overlap)(void*)){
	snode *update[MAX_L+1];
	snode *x=list->header;
	
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) 
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];

	run_t *t_r=(run_t*)value;
#if defined(KVSSD) && defined(Lsmtree)
//	if(KEYCMP(key,list->start)<0) list->start=key;
//	if(KEYCMP(key,list->end)>0) list->end=key;
	if(KEYTEST(key,x->key))
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
		if(overlap)
			overlap((void*)x->value);
		x->value=(value_set*)value;
		t_r->run_data=(void*)x;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
		//x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

		x->key=key;
		x->ppa=UINT_MAX;
		x->value=(value_set*)value;
		t_r->run_data=(void*)x;

		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}

		//new back
		x->back=x->list[1]->back;
		x->list[1]->back=x;

		x->level=level;
		list->size++;
	}
	return x;
}

skiplist *skiplist_cutting_header(skiplist *in,uint32_t *value){
	static uint32_t num_limit=KEYBITMAP/sizeof(uint16_t)-2;
	static uint32_t size_limit=PAGESIZE-KEYBITMAP;
	if(in->all_length<size_limit && in->size <num_limit) return in;

	uint32_t length=0;
	uint32_t idx=0;
	snode *temp;
	for_each_sk(temp,in){
		length+=KEYLEN(temp->key);
		idx++;
		if(length+KEYLEN(temp->list[1]->key)>=size_limit || idx>=num_limit ) break;
	}
	skiplist *res=skiplist_divide(in,temp);
	res->size=idx;
	res->all_length=length;
	in->size-=idx;
	in->all_length-=length;
	*value=idx;
	return res;
}

skiplist *skiplist_cutting_header_se(skiplist *in,uint32_t *value,KEYT *start, KEYT *end){
	static uint32_t num_limit=KEYBITMAP/sizeof(uint16_t)-2;
	static uint32_t size_limit=PAGESIZE-KEYBITMAP;
	snode *temp;
	uint32_t length=0;
	uint32_t idx=0;
	KEYT t_end;
	if(in->all_length<size_limit && in->size <num_limit){
		for_each_sk(temp,in){
			if(idx==0){
				kvssd_cpy_key(start,&temp->key);
			}
			t_end=temp->key;
			idx++;
		}
		if(idx!=0){
			kvssd_cpy_key(end,&temp->key);
		}
		return in;
	}

	for_each_sk(temp,in){
		if(idx==0){
			kvssd_cpy_key(start,&temp->key);
		}
		length+=KEYLEN(temp->key);
		idx++;
		t_end=temp->key;
		if(length+KEYLEN(temp->list[1]->key)>=size_limit || idx>=num_limit ) break;
	}
	kvssd_cpy_key(end,&t_end);
	skiplist *res=skiplist_divide(in,temp);
	res->size=idx;
	res->all_length=length;
	in->size-=idx;
	in->all_length-=length;
	*value=idx;
	return res;
}

#endif
snode *skiplist_insert_iter(skiplist *list,KEYT key,ppa_t ppa){
	snode *update[MAX_L+1];
	snode *x=list->header;

	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) 
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];
#if defined(KVSSD)
	if(KEYTEST(key,x->key))
#else
	if(key<list->start) list->start=key;
	if(key>list->end) list->end=key;
	if(key==x->key)
#endif
	{
#ifdef DEBUG

#endif
		x->ppa=ppa;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
	//	x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

		x->key=key;

		x->ppa=ppa;
		x->value=NULL;
		list->all_length+=key.len;
#ifdef Lsmtree
		x->iscaching_entry=false;
#endif

#ifdef demand
		x->lpa = UINT32_MAX;
		x->hash_params = NULL;
		x->params = NULL;
#endif
		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}

		//new back
		x->back=x->list[1]->back;
		x->list[1]->back=x;

		x->level=level;
		list->size++;
	}
	return x;
}
//extern bool testflag;
snode *skiplist_insert(skiplist *list,KEYT key,value_set* value, bool deletef){
	snode *update[MAX_L+1];
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) 
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];
	if(value!=NULL){
		value->length=(value->length/PIECE)+(value->length%PIECE?1:0);
	}
#if defined(KVSSD)
	if(KEYTEST(key,x->key))
#else
	if(key==x->key)
#endif
	{
#ifdef DEBUG

#endif
	//	algo_req * old_req=x->req;
	//	lsm_params *old_params=(lsm_params*)old_req->params;
	//	old_params->lsm_type=OLDDATA;
		/*
		static int cnt=0;
		if(testflag){
			printf("%d overlap!\n",++cnt);
		}*/
		list->data_size-=(x->value->length*PIECE);
		list->data_size+=(value->length*PIECE);
		if(x->value)
			inf_free_valueset(x->value,FS_MALLOC_W);
#if defined(KVSSD)
		free(key.key);
#endif
	//	old_req->end_req(old_req);

		x->value=value;
		x->isvalid=deletef;
		return x;
	}
	else{
		int level=getLevel();
		if(level>list->level){
			for(int i=list->level+1; i<=level; i++){
				update[i]=list->header;
			}
			list->level=level;
		}
#ifdef USINGSLAB
	//	x=(snode*)slab_alloc(&snode_slab);
		x=(snode*)kmem_cache_alloc(snode_slab,KM_NOSLEEP);
#else
		x=(snode*)malloc(sizeof(snode));
#endif
		x->list=(snode**)malloc(sizeof(snode*)*(level+1));

		x->key=key;
		x->isvalid=deletef;

		x->ppa=UINT_MAX;
		x->value=value;

#ifdef KVSSD
		list->all_length+=KEYLEN(key);
#endif

#ifdef Lsmtree
		x->iscaching_entry=false;
#endif

#ifdef demand
		x->lpa = UINT32_MAX;
		x->hash_params = NULL;
		x->params = NULL;
#endif

		for(int i=1; i<=level; i++){
			x->list[i]=update[i]->list[i];
			update[i]->list[i]=x;
		}

		//new back
		x->back=x->list[1]->back;
		x->list[1]->back=x;

		x->level=level;
		list->size++;
		list->data_size+=(value->length*PIECE);
	}
	return x;
}

#ifdef Lsmtree
//static int make_value_cnt=0;
value_set **skiplist_make_valueset(skiplist *input, level *from,KEYT *start, KEYT *end){
	value_set **res=(value_set**)malloc(sizeof(value_set*)*(input->size+1));
	memset(res,0,sizeof(value_set*)*(input->size+1));
	l_bucket b;
	memset(&b,0,sizeof(b));
	uint32_t idx=1;
	snode *target;
	int total_size=0;
	for_each_sk(target,input){
		if(idx==1){
			kvssd_cpy_key(start,&target->key);
		}
		else if (idx==input->size){
			kvssd_cpy_key(end,&target->key);
		}
		idx++;

		if(target->value==0) continue;
		if(b.bucket[target->value->length]==NULL){
			b.bucket[target->value->length]=(snode**)malloc(sizeof(snode*)*(input->size+1));
		}
		b.bucket[target->value->length][b.idx[target->value->length]++]=target;
		total_size+=target->value->length;

	}
	int res_idx=0;
	for(int i=0; i<b.idx[PAGESIZE/PIECE]; i++){//full page
		target=b.bucket[PAGESIZE/PIECE][i];
		res[res_idx]=target->value;
		res[res_idx]->ppa=LSM.lop->moveTo_fr_page(false);//real physical index
		target->ppa=LSM.lop->get_page((PAGESIZE/PIECE),target->key);

		footer *foot=(footer*)pm_get_oob(CONVPPA(target->ppa),DATA,false);
		foot->map[0]=NPCINPAGE;

		target->value=NULL;
		res_idx++;
	}
	b.idx[PAGESIZE/PIECE]=0;
	
	for(int i=1; i<PAGESIZE/PIECE+1; i++){
		if(b.idx[i]!=0)
			break;
		if(i==PAGESIZE/PIECE){
			return res;
		}
	}

#ifdef DVALUE
	variable_value2Page(from,&b,&res,&res_idx,false);
#endif

	for(int i=0; i<=NPCINPAGE; i++){
		if(b.bucket[i]) free(b.bucket[i]);
	}
	res[res_idx]=NULL;
	return res;
}
#endif

snode *skiplist_at(skiplist *list, int idx){
	snode *header=list->header;
	for(int i=0; i<idx; i++){
		header=header->list[1];
	}
	return header;
}

int skiplist_delete(skiplist* list, KEYT key){
	if(list->size==0)
		return -1;
	snode *update[MAX_L+1];
	snode *x=list->header;
	for(int i=list->level; i>=1; i--){
#if defined(KVSSD) 
		while(KEYCMP(x->list[i]->key,key)<0)
#else
		while(x->list[i]->key<key)
#endif
			x=x->list[i];
		update[i]=x;
	}
	x=x->list[1];

#if defined(KVSSD) 
	if(KEYCMP(x->key,key)!=0)
#else
	if(x->key!=key)
#endif
		return -2; 

	for(int i=x->level; i>=1; i--){
		update[i]->list[i]=x->list[i];
		if(update[i]==update[i]->list[i])
			list->level--;
	}

//   inf_free_valueset(x->value, FS_MALLOC_W);
	free(x->list);
#ifdef USINGSLAB
	//slab_free(&snode_slab,x);
	kmem_cache_free(snode_slab,x);
#else
	free(x);
#endif
	list->size--;
	return 0;
}

sk_iter* skiplist_get_iterator(skiplist *list){
	sk_iter *res=(sk_iter*)malloc(sizeof(sk_iter));
	res->list=list;
	res->now=list->header;
	return res;
}

snode *skiplist_get_next(sk_iter* iter){
	if(iter->now->list[1]==iter->list->header){ //end
		return NULL;
	}
	else{
		iter->now=iter->now->list[1];
		return iter->now;
	}
}
// for test
void skiplist_dump(skiplist * list){
	sk_iter *iter=skiplist_get_iterator(list);
	snode *now;
	while((now=skiplist_get_next(iter))!=NULL){
		for(uint32_t i=1; i<=now->level; i++){
#if defined(KVSSD) 
#else
			printf("%u ",now->key);
#endif
		}
		printf("\n");
	}
	free(iter);
}

void skiplist_clear(skiplist *list){
	snode *now=list->header->list[1];
	snode *next=now->list[1];
	while(now!=list->header){

		if(now->value){
			inf_free_valueset(now->value,FS_MALLOC_W);//not only length<PAGESIZE also length==PAGESIZE, just free req from inf
		}
		free(now->key.key);
		free(now->list);
#ifdef USINGSLAB
	//	slab_free(&snode_slab,now);
		kmem_cache_free(snode_slab,now);
#else
		free(now);
#endif
		now=next;
		next=now->list[1];
	}
	list->size=0;
	list->level=0;
	for(int i=0; i<MAX_L; i++) list->header->list[i]=list->header;
#if defined(KVSSD) 
	list->header->key=key_max;
#else
	list->header->key=INT_MAX;
#endif
}
skiplist *skiplist_copy(skiplist* src){
	skiplist* des=skiplist_init();
	snode *now=src->header->list[1];
	snode *n_node;
	while(now!=src->header){
		n_node=skiplist_insert(des,now->key,now->value,now->isvalid);
		n_node->ppa=now->ppa;
		now=now->list[1];
	}

	return des;
}
#ifdef Lsmtree
skiplist *skiplist_merge(skiplist* src, skiplist *des){
	snode *now=src->header->list[1];
	while(now!=src->header){
		skiplist_insert_wP(des,now->key,now->ppa,now->isvalid);
		now=now->list[1];
	}
	return des;
}
#endif

void skiplist_free(skiplist *list){
	if(list==NULL) return;
	skiplist_clear(list);
	free(list->header->list);
#ifdef USINGSLAB
	//slab_free(&snode_slab,list->header);
	kmem_cache_free(snode_slab,list->header);
#else
	free(list->header);
#endif
	free(list);
	return;
}

void skiplist_container_free(skiplist *list){
	if(list==NULL) return;
	snode *now=list->header->list[1];
	snode *next=now->list[1];
	while(now!=list->header){
		free(now->list);
		if(now->value){
			inf_free_valueset(now->value,FS_MALLOC_W);//not only length<PAGESIZE also length==PAGESIZE, just free req from inf
			free(now->key.key);
		}
#ifdef Lsmtree
	//	if(now->iscaching_entry)
	//		free(now->key.key);
#endif

#ifdef USINGSLAB
		kmem_cache_free(snode_slab,now);
#else
		free(now);
#endif
		now=next;
		next=now->list[1];
	}
	list->size=0;
	list->level=0;

	free(list->header->list);
#ifdef USINGSLAB
	//slab_free(&snode_slab,list->header);
	kmem_cache_free(snode_slab,list->header);
#else
	free(list->header);
#endif
	free(list);
}
snode *skiplist_pop(skiplist *list){
	if(list->size==0) return NULL;
	KEYT key=list->header->list[1]->key;
	int i;
	snode *update[MAX_L+1];
	snode *x=list->header;
	for(i=list->level; i>=1; i--){
		update[i]=list->header;
	}
	x=x->list[1];
#if defined(KVSSD) 
	if(KEYCMP(x->key,key)==0)
#else
	if(x->key==key)
#endif
	{
		for(i =1; i<=list->level; i++){
			if(update[i]->list[i]!=x)
				break;
			update[i]->list[i]=x->list[i];
		}

		while(list->level>1 && list->header->list[list->level]==list->header){
			list->level--;
		}
		list->all_length-=KEYLEN(key);
		list->size--;
		return x;
	}
	return NULL;	
}

void skiplist_save(skiplist *input){
	return;
}
skiplist *skiplist_load(){
	skiplist *res=skiplist_init();
	return res;
}

void skiplist_print(skiplist *skip){
	snode *temp;
	
	for_each_sk(temp,skip){
		printf("[lev:%d]%p\t",temp->level,temp);
		for(uint32_t i=0; i<temp->level; i++){
			printf("[%.*s] ", temp->key.len,temp->key.key);
		}
		printf("\n");
	}

	printf("max level ptr:");
	for(uint32_t i=skip->level; i>=1; i--){
		printf("%p ",skip->header->list[i]);
	}
	printf("\n");
	
}

skiplist *skiplist_divide(skiplist *in, snode *target){
	skiplist *res=skiplist_init();
	if(target==in->header){
		skiplist swap;
		memcpy(&swap,in,sizeof(skiplist));
		memcpy(in,res,sizeof(skiplist));
		memcpy(res,&swap,sizeof(skiplist));
		return res;
	}
	uint32_t origin_level=in->level;
	res->level=in->level;

	snode *x=in->header->list[res->level],*temp,*temp2;
	uint32_t t_level=target==in->header?1:target->level;
	for(uint32_t i=res->level; i>t_level; i--){
		while(KEYCMP(x->list[i]->key,target->key)<0)
			x=x->list[i];

		if(KEYCMP(x->key,target->key)>0){
			res->level--;
			x=in->header->list[i-1];
			continue;
		}
		else if(x->list[i]==in->header){
			in->level--;
		}

		temp=in->header->list[i];
		temp2=x;

		in->header->list[i]=x->list[i];
		res->header->list[i]=temp;
		temp2->list[i]=res->header;

	}

	for(uint32_t i=t_level; i>=1; i--){
		res->header->list[i]=in->header->list[i];
		in->header->list[i]=target->list[i];
		if(target->list[i]==in->header){
			in->level--;
		}
		target->list[i]=res->header;
	}

	
	if((origin_level!=in->level && origin_level!=res->level) ||(in->level!=0 && (in->header->list[in->level]==in->header || res->header->list[res->level]==res->header))){
		printf("origin_level:%d in->level:%d\n",origin_level,in->level);
		printf("res->header->list[res->level] %p, res->header %p\n",res->header->list[res->level],res->header);
		printf("in->header->list[in->level] %p, in->header %p\n",in->header->list[in->level],in->header);
		printf("skiplist_divide error!\n");
		abort();
	}
	if(in->level==0) in->level=1;

	return res;
}

uint32_t skiplist_memory_size(skiplist *skip){
	if(!skip) return 0;
	uint32_t res=0;
	snode *temp;
	for_each_sk(temp,skip){
		res+=sizeof(snode)+temp->level*sizeof(snode*);
		res+=temp->key.len;
		res+=sizeof(temp->key);
	}
	return res;
}

