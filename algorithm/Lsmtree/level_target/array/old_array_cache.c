#include "array.h"
#include "../../level.h"
#include "../../bloomfilter.h"
#include "../../lsmtree.h"
#include "../../../../interface/interface.h"
#include "../../../../include/utils/kvssd.h"
#include "../../../../include/settings.h"
#include "array.h"

void array_cache_insert(level *lev,skiplist* mem){
	array_body *b=(array_body*)lev->level_data;
	skiplist *skip=b->skip;
	
	uint32_t idx=1;
	snode *temp;
	snode *new_node;
	for_each_sk(temp,mem){
		if(idx==1){
			array_range_update(lev,NULL,temp->key);
		}
		else if(idx==mem->size){
			array_range_update(lev,NULL,temp->key);
		}
		new_node=skiplist_insert_existIgnore(skip,temp->key,temp->ppa,temp->ppa==UINT32_MAX?false:true);
		if(new_node->key.key==temp->key.key){
			temp->key.key=NULL; //mem will be freed
		}
		new_node->iscaching_entry=true;
		idx++;
	}
}

void array_cache_merge(level *src, level *des){
	array_body *s=(array_body*)src->level_data;
	array_body *d=(array_body*)des->level_data;

	snode *temp;

	KEYT start=s->skip->header->list[1]->key,end;
//	int cnt_s=1;
	snode *new_snode;
	for_each_sk(temp,s->skip){
		end=temp->key;
		new_snode=skiplist_insert_existIgnore(d->skip,temp->key,temp->ppa,temp->ppa==UINT32_MAX?false:true);
		if(new_snode->key.key!=temp->key.key){
			if(temp->key.key==start.key){
				start=new_snode->key;
			}
			if(temp->key.key==end.key){
				end=new_snode->key;
			}
			free(temp->key.key);
		}
		new_snode->iscaching_entry=true;
	}
	array_range_update(des,NULL,start);
	array_range_update(des,NULL,end);
}

void array_cache_free(level *lev){
	array_body *b=(array_body*)lev->level_data;
	skiplist_container_free(b->skip);
	b->skip=NULL;
}


void array_cache_move(level *src, level *des){
	if(array_cache_get_sz(src)==0)
		return;
	//array_cache_merge(src,des);
	array_body *s=(array_body*)src->level_data;
	array_body *d=(array_body*)des->level_data;
	
	skiplist_free(d->skip);
	d->skip=s->skip;
	s->skip=NULL;
	
	des->start=src->start;
	des->end=src->end;
}

keyset *array_cache_find(level *lev, KEYT lpa){
	array_body *b=(array_body*)lev->level_data;
	snode *target=skiplist_find(b->skip,lpa);
	if(target)
		return (keyset*)target;
	else return NULL;
}

char *array_cache_find_run_data(level *lev,KEYT lpa){
	array_body *b=(array_body*)lev->level_data;
	snode *temp=skiplist_strict_range_search(b->skip,lpa);
	if(temp==NULL) return NULL;
	return make_rundata_from_snode(temp);
}

char *array_cache_next_run_data(level *lev, KEYT lpa){
	array_body *b=(array_body*)lev->level_data;
	snode *temp=skiplist_strict_range_search(b->skip,lpa);
	if(temp==NULL) return NULL;
	temp=temp->list[1];
	return make_rundata_from_snode(temp);
}

char *array_cache_find_lowerbound(level *lev, KEYT lpa, KEYT *start, bool datareturn){
	array_body *b=(array_body*)lev->level_data;
	snode *temp=skiplist_find_lowerbound(b->skip,lpa);
	if(temp==b->skip->header) {
		start->key=NULL;
		return NULL;
	}
	start->key=temp->key.key;
	start->len=temp->key.len;
	if(datareturn)
		return make_rundata_from_snode(temp);
	else return NULL;
}

int array_cache_get_sz(level* lev){
	array_body *b=(array_body*)lev->level_data;
	if(!b->skip) return 0;
	int t=(PAGESIZE-KEYBITMAP);
	int nbl=(b->skip->all_length/t)+(b->skip->all_length%t?1:0);
	int nbs=(b->skip->size)/(KEYBITMAP/sizeof(uint16_t))+(b->skip->size%(KEYBITMAP/sizeof(uint16_t))?1:0);
	return nbs<nbl?nbl:nbs;
}

skiplist* array_cache_get_body(level *lev){
	array_body *b=(array_body*)lev->level_data;
	return b->skip;
}

lev_iter *array_cache_get_iter(level *lev,KEYT from, KEYT to){
	array_body *b=(array_body*)lev->level_data;
	lev_iter *it=(lev_iter*)malloc(sizeof(lev_iter));
	it->from=from;
	it->to=to;

	c_iter *iter=(c_iter*)malloc(sizeof(c_iter));
	iter->body=b->skip;
	iter->temp=skiplist_find_lowerbound(b->skip,from);
	iter->last=skiplist_find_lowerbound(b->skip,to);
	iter->last=iter->last->list[1];

	iter->isfinish=false;
	it->iter_data=(void*)iter;
	it->lev_idx=lev->idx;
	return it;
}

run_t *array_cache_iter_nxt(lev_iter *it){
	c_iter *iter=(c_iter*)it->iter_data;
	if(iter->isfinish){;
		free(iter);
		free(it);
		return NULL;
	}

	htable *res=LSM.nocpy?htable_assign(NULL,0):htable_assign(NULL,1);

	KEYT start,end;
	char *ptr=(char*)res->sets;
	uint16_t *bitmap=(uint16_t*)ptr;
	uint32_t idx=1;
	memset(bitmap,-1,KEYBITMAP/sizeof(uint16_t));
	uint16_t data_start=KEYBITMAP;
	uint32_t length=0;
	snode *temp=iter->temp;
	start=temp->key;
	uint16_t cnt=0;
	do{
		memcpy(&ptr[data_start],&temp->ppa,sizeof(temp->ppa));
		memcpy(&ptr[data_start+sizeof(temp->ppa)],temp->key.key,temp->key.len);
		bitmap[idx]=data_start;

		data_start+=temp->key.len+sizeof(temp->ppa);
		length+=KEYLEN(temp->key);
		idx++;
		end=temp->key;
		temp=temp->list[1];
		cnt++;
	}
	while(temp!=iter->last && (length+KEYLEN(temp->key)<=PAGESIZE-KEYBITMAP) && (cnt<KEYBITMAP/sizeof(uint16_t)-2));
	bitmap[0]=idx-1;
	bitmap[idx]=data_start;
	
	if(temp==iter->last){
		iter->isfinish=true;
	}
	iter->temp=temp;

	run_t *res_r=array_make_run(start,end,-1);
	res_r->cpt_data=res;
	return res_r;
}
