#include "../../level.h"
#include "../../bloomfilter.h"
#include "../../lsmtree.h"
#include "../../../../interface/interface.h"
#include "hash_table.h"
#include "../../../../include/utils/thpool.h"
extern lsmtree LSM;
extern KEYT key_max,key_min;
level_ops h_ops={
	.init=hash_init,
	.release=hash_free,
	.insert=hash_insert,
	.find_keyset=hash_find_keyset,
	.find_keyset_first=NULL,
	.find_keyset_last=NULL,
	.full_check=def_fchk,
	.tier_align=hash_tier_align,
	.move_heap=def_move_heap,
	.chk_overlap=hash_chk_overlap,
	.range_find=hash_range_find,
	.range_find_compaction=hash_range_find,
	.unmatch_find=hash_unmatch_find,
	//.range_find_start=hash_range_find_start,
	//.range_find_nxt_node=NULL,
	.next_run=NULL,
	.get_iter=hash_get_iter,
	.iter_nxt=hash_iter_nxt,
	.get_max_table_entry=h_max_table_entry,
	.get_max_flush_entry=h_max_flush_entry,

	.keyset_iter_init=NULL,
	.keyset_iter_nxt=NULL,
	.mem_cvt2table=hash_mem_cvt2table,
#ifdef STREAMCOMP
	.stream_merger=hash_stream_merger,
	.stream_comp_wait=hash_stream_comp_wait,
#else
	.merger=hash_merger_wrapper,
#endif
	.cutter=hash_cutter,
#ifdef BLOOM
	.making_filter=hash_making_filter,
#endif
	.make_run=hash_make_run,
	.find_run=hash_find_run,
	.release_run=hash_free_run,
	.run_cpy=hash_run_cpy,

	.moveTo_fr_page=def_moveTo_fr_page,
	.get_page=def_get_page,
	.block_fchk=def_blk_fchk,
	.range_update=hash_range_update,

#ifdef LEVELCACHING
	.cache_insert=hash_cache_insert,
	.cache_merge=hash_cache_merge,
	.cache_free=hash_cache_free,
	.cache_comp_formatting=hash_cache_comp_formatting,
	.cache_move=hash_cache_move,
	.cache_find=hash_cache_find,
	//.cache_find_run=hash_cache_find_run,
	.cache_find_run_data=NULL,
	.cache_next_run_data=NULL,
	.cache_get_iter=NULL,
	.cache_get_size=hash_cache_get_sz,
#endif
	.print=hash_print,
	.all_print=hash_all_print,
	.header_print=NULL,
};

#ifdef STREAMCOMP
static bool start_flag=0;
threadpool stream_compactor;
#endif
uint32_t hash_insert_cnt;

static inline hash *r2h(run_t* a){
#ifdef KVSSD
	char *ptr=(char*)a->cpt_data->sets;
	return (hash*)&ptr[sizeof(KEYT)];
#else
	return (hash*)a->cpt_data->sets;
#endif
}

static inline hash* r2h_from_compaction(run_t *a){
#ifdef NOCPY
	if(a->cpt_data->nocpy_table)
		return (hash*)a->cpt_data->nocpy_table;
#endif
	return (hash*)a->cpt_data->sets;
}

void hash_overlap(void *value){
	if(!value)return;
	//printf("%p free\n",value);
	run_t *temp=(run_t*)value;
	htable_free(temp->cpt_data);
	hash_free_run(temp);
}

static bool hash_body_insert(hash *c,keyset input){
	if(c->n_num>=CUC_ENT_NUM){
		return false;
	}else{
		uint32_t h_keyset=f_h(input.lpa);
		uint32_t idx=0,i=0,try_n=0;
		while(1){
			try_n++;
			idx=(h_keyset+i*i+i)%(HENTRY);
#ifdef KVSSD
			if(c->b[idx].lpa.key==NULL){
				if(c->t_num<try_n)c->t_num=try_n;
				c->b[idx]=input;
				c->n_num++;
				return true;
			}
			else if(KEYCMP(c->b[idx].lpa,input.lpa)==0){
//			else if(memcmp(c->b[idx].lpa.key,input.lpa.key,c->b[idx].lpa.len>input.lpa.len?input.lpa.len:c->b[idx].lpa.len)==0){
				invalidate_PPA(c->b[idx].ppa);
				c->b[idx].ppa=input.ppa;
				return true;
			}
#else
			if(c->b[idx].lpa==UINT_MAX){
				if(c->t_num<try_n)c->t_num=try_n;
				c->b[idx]=input;
				c->n_num++;
				return true;
			}else if(c->b[idx].lpa==input.lpa){
				invalidate_PPA(c->b[idx].ppa);
				c->b[idx].ppa=input.ppa;
				return true;
			}
#endif
			i++;
		}
		return false;
	}
}
static bool hash_real_insert(run_t *r, keyset input,float fpr){
	hash *c=r2h(r);
	if(c->n_num>=CUC_ENT_NUM){
		return false;
	}else{
		hash_range_update(NULL,r,input.lpa);
#ifdef BLOOM
		if(r->filter==NULL){
			r->filter=bf_init(HENTRY,fpr);
		}
		bf_set(r->filter,input.lpa);
#endif

#ifdef KVSSD
		if(KEYCMP(r->key,input.lpa)>0){
			r->key=input.lpa;
		}
		if(KEYCMP(r->end,input.lpa)<0){
			r->end=input.lpa;
		}
#else
		if(r->key>input.lpa) r->key=input.lpa;
		if(r->end<input.lpa) r->end=input.lpa;
#endif
		hash_body_insert(c,input);
	}
	return true;
}
#ifdef KVSSD
int keyset_cmp(const void *_a, const void *_b){
	keyset *a=(keyset*)_a;
	keyset *b=(keyset*)_b;
	
	if(a->lpa.key==NULL && b->lpa.key!=NULL){
		return -1;
	}else if(b->lpa.key==NULL && a->lpa.key!=NULL){
		return 1;
	}else if(b->lpa.key==NULL && a->lpa.key==NULL) return 0;
	return KEYCMP(a->lpa,b->lpa);
}
#endif
static KEYT hash_split(run_t *_src, run_t *_a_des, run_t* _b_des, float fpr){
	hash *src=r2h(_src);
	KEYT partition;
#ifdef KVSSD
	uint32_t valid_num=src->n_num;
	qsort(src->b,HENTRY,sizeof(keyset),keyset_cmp);
	partition=src->b[valid_num/2].lpa;
	for(uint32_t i=0; i<valid_num; i++){
		if(KEYCMP(src->b[i].lpa,partition)<0){
			hash_real_insert(_a_des,src->b[i],fpr);
		}else{
			hash_real_insert(_b_des,src->b[i],fpr);
		}
	}
#else
	partition=(_src->key+_src->end)/2;
	for(uint32_t i=0; i<HENTRY; i++){
		if(src->b[i].lpa==UINT_MAX) continue;
		if(src->b[i].lpa<partition){
			hash_real_insert(_a_des,src->b[i],fpr);
		}
		else{
			hash_real_insert(_b_des,src->b[i],fpr);
		}
	}
#endif
	return partition;
}

static run_t *hash_make_dummy_run(){
	run_t *res;
#ifdef KVSSD
	res=hash_make_run(key_max,key_min,-1);
#else
	res=hash_make_run(UINT_MAX,0,-1);
#endif

	res->cpt_data=htable_assign(NULL,0);
#ifdef KVSSD
	res->cpt_data->sets[0].ppa=0;
#else
	res->cpt_data->sets[0].lpa=res->cpt_data->sets[0].ppa=0;
#endif
	return res;
}
extern bool flag_value;
static void hash_insert_into(hash_body *b, keyset input, float fpr){
	run_t *h;
	hash_insert_cnt++;

	h=b->late_use_node?b->late_use_node:b->temp;
	if(b->late_use_nxt && b->late_use_node){
		run_t *h2=b->late_use_nxt;
#ifdef KVSSD
		if(KEYCMP(input.lpa,h->key)<0 || (b->late_use_nxt!=b->late_use_node && !(KEYCMP(h->key,input.lpa)<=0 && KEYCMP(input.lpa,h2->key)<0)))
#else
		if(input.lpa< h->key || (b->late_use_nxt!=b->late_use_node &&!(h->key<=input.lpa && input.lpa< h2->key)))
#endif		
		{
			snode* s=skiplist_range_search(b->body,input.lpa);

			while(s && s!=b->body->header){
				run_t *check=(run_t*)s->value;
				if(check->cpt_data==NULL){
					s=s->list[1];
				}else{
					break;
				}
			}
			if(s==b->body->header){
				DEBUG_LOG("fuck");
			}
			h=b->late_use_node=(run_t*)s->value;
			if(s->list[1]!=b->body->header)
				b->late_use_nxt=(run_t*)s->list[1]->value;
			else //when the late_use_node is last node of skiplist
				b->late_use_nxt=h;
		}
	}else{
		b->late_use_node=b->temp;
		b->late_use_nxt=NULL;
	}

	if(!hash_real_insert(h,input,fpr)){
		if(h==b->temp) b->temp=NULL;
		if(!b->body) b->body=skiplist_init();
		snode *w,*e;
		run_t *new_hash=hash_make_dummy_run();
		run_t *new_hash2=hash_make_dummy_run();
		KEYT partition=hash_split(h,new_hash2,new_hash,fpr);
		if(h->cpt_data){
			htable_free(h->cpt_data);
			h->cpt_data=NULL;
		}
		skiplist_delete(b->body,h->key);
		hash_free_run(h);
		h=new_hash2;
		skiplist_general_insert(b->body,h->key,(void*)h,hash_overlap);
		w=skiplist_general_insert(b->body,new_hash->key,(void*)new_hash,hash_overlap);


		e=w->list[1];
#ifdef KVSSD
		if(KEYCMP(input.lpa,partition)<0)
#else
		if(input.lpa<partition)
#endif
		{
			hash_real_insert(h,input,fpr);
			b->late_use_node=h;
			b->late_use_nxt=(run_t*)w->value;
		}else{
			hash_real_insert(new_hash,input,fpr);
			b->late_use_node=new_hash;
			if(e!=b->body->header)
				b->late_use_nxt=(run_t*)e->value;
			else
				b->late_use_nxt=(run_t*)w->value;
		}
	}
}

void hash_merger_wrapper(struct skiplist* mem, run_t** s, run_t** o, struct level* d){
#ifdef STREAMCOMP
	hash_merger(mem,s,o,d,true);
#else
	hash_merger(mem,s,o,d,false);
#endif
}
#ifdef STREAMCOMP
pthread_t tid;
void hash_merger_thread_func(void *args,int id){
	void **arg=(void**)args;
	run_t **s=(run_t**)arg[0];
	run_t **o=(run_t**)arg[1];
	level *d=(level*)arg[2];
	skiplist *mem=(skiplist*)arg[3];

	hash_merger_wrapper(mem,s,o,d);

	free(arg);
	return;
}
void hash_stream_merger(skiplist* mem,run_t** src, run_t** org,  level *des){
	if(!start_flag){
		start_flag=1;
		stream_compactor=thpool_init(1);
	}
	void **args=(void **)malloc(sizeof(void*)*4);
	args[0]=(void*)src;
	args[1]=(void*)org;
	args[2]=(void*)des;
	args[3]=(void*)mem;

	while(thpool_num_threads_working(stream_compactor)>=1);
	thpool_add_work(stream_compactor,hash_merger_thread_func,(void*)args);
}

void hash_stream_comp_wait(){
	thpool_wait(stream_compactor);
}
#endif
void hash_merger(struct skiplist* mem, run_t** s, run_t** o, struct level* d, bool waiting){
	hash_body *des=(hash_body*)d->level_data;
	if(des->body){
		snode *t;
		for_each_sk(t,des->body){
			//printf("t->key:%d\n",t->key);
		}
	}
	if(!des->temp){
		des->temp=hash_make_dummy_run();
		des->late_use_node=NULL;
	}
	for(int i=0; o[i]!=NULL; i++){
		if(waiting){
			while(!o[i]->cpt_data ||!o[i]->cpt_data->done){}
		}
		hash *h=r2h_from_compaction(o[i]);
		for(int j=0; j<HENTRY; j++){
#ifdef KVSSD
			if(h->b[j].lpa.key==NULL) continue;
#else
			if(h->b[j].lpa==UINT_MAX) continue;
#endif
			if(h->b[j].ppa==UINT_MAX && d->idx==LEVELN-1) continue; //delete req finally arrive at last level
			hash_insert_into(des,h->b[j],d->fpr);
			hash_range_update(d,NULL,h->b[j].lpa);
		}
	}

	if(mem){
		keyset target;
		snode *s=mem->header->list[1];
		while(s!=mem->header){
			target.lpa=s->key;
			target.ppa=s->ppa;
			hash_insert_into(des,target,d->fpr);
			hash_range_update(d,NULL,target.lpa);
			s=s->list[1];
		}
	}else{
		for(int i=0; s[i]!=NULL; i++){
			if(waiting){
				while(!s[i]->cpt_data || !s[i]->cpt_data->done){}
			}
			hash *h=r2h_from_compaction(s[i]);
			//htable_check(s[i]->cpt_data,0,81665,"s:");
			for(int j=0; j<HENTRY; j++){
#ifdef KVSSD	
				if(h->b[j].lpa.key==NULL) continue;
#else
				if(h->b[j].lpa==UINT_MAX) continue;
#endif
				hash_insert_into(des,h->b[j],d->fpr);	
				hash_range_update(d,NULL,h->b[j].lpa);
			}
		}
	}
	d->n_num=des->body->size;
}

run_t *hash_cutter(struct skiplist* mem, struct level* d, KEYT* start, KEYT *end){
	hash_body *des=(hash_body*)d->level_data;
	static struct lev_iter *iter=NULL;
	iter=(!iter)?hash_get_iter(d,des->body->start,des->body->end):iter;

	run_t *r;
	while((r=hash_iter_nxt(iter))){
		if(!r->cpt_data) continue;
		return r;
	}
	iter=NULL;
	return NULL;
}

htable *hash_mem_cvt2table(skiplist *mem,run_t* input){
	value_set *temp=inf_get_valueset(NULL,FS_MALLOC_W,PAGESIZE);
	htable *res=(htable*)malloc(sizeof(htable));
	res->t_b=FS_MALLOC_W;

#ifdef NOCPY
	res->sets=(keyset*)malloc(PAGESIZE);
#else
	res->sets=(keyset*)temp->value;
#endif
	res->origin=temp;

#ifdef BLOOM
	BF *filter=bf_init(h_max_table_entry(),LSM.disk[0]->fpr);
	input->filter=filter;
#endif
	input->cpt_data=res;

	snode *target;
	keyset t_set;
	hash *t_hash=(hash*)res->sets;
#ifdef KVSSD
	memset(res->sets,0,PAGESIZE);
	t_hash->b=&res->sets[1];
#else
	memset(res->sets,-1,PAGESIZE);
	t_hash->n_num=t_hash->t_num=0;
#endif

	int idx=0;
	for_each_sk(target,mem){
		t_set.lpa=target->key;
		t_set.ppa=target->isvalid?target->ppa:UINT_MAX;
#ifdef BLOOM
		bf_set(filter,t_set.lpa);
#endif
		idx++;
		hash_body_insert(t_hash,t_set);
	}
	return res;
}

bool hash_chk_overlap(struct level *lev, KEYT start, KEYT end){
#ifdef KVSSD
	if(KEYCMP(lev->start,end)>0 || KEYCMP(lev->end,start)<0)
#else
	if(lev->start > end || lev->end < start)
#endif
	{
		return false;
	}
	return true;
}

void hash_tier_align(struct level *){
	printf("it is empty\n");
}

BF* hash_making_filter(run_t *data, float fpr){
	hash *h=r2h_from_compaction(data);
	BF *res=bf_init(LSM.KEYNUM,fpr);
	for(int i=0; i<HENTRY; i++){
#ifdef KVSSD
		if(h->b[i].lpa.key==NULL) continue;
#else
		if(h->b[i].lpa==UINT_MAX) continue;
#endif
		bf_set(res,h->b[i].lpa);
	}
	return res;
}

#ifdef LEVELCACHING
static inline hash_body *cfl(level *lev){
	hash_body *h=(hash_body*)lev->level_data;
	return h->lev_cache_data;
}


void hash_cache_insert(level *lev,run_t* r){
	hash_body *lc=cfl(lev);
	if(lc==NULL){
		lc=(hash_body*)calloc(sizeof(hash_body),1);
		lc->temp=hash_make_dummy_run();
		lc->late_use_node=NULL;
	}

	hash *h=r2h(r);
	for(int i=0; i<HENTRY; i++){
#ifdef KVSSD
		if(h->b[i].lpa.key==NULL) continue;
#else
		if(h->b[i].lpa==UINT_MAX) continue;
#endif
		hash_insert_into(lc,h->b[i],lev->fpr);
		hash_range_update(lev,NULL,h->b[i].lpa);
	}
	((hash_body*)lev->level_data)->lev_cache_data=lc;
}

void hash_cache_merge(level *src,level * des){
	hash_body *slc=cfl(src);
	hash_body *dlc=cfl(des);

	if(dlc==NULL){
		dlc=(hash_body*)calloc(sizeof(hash_body),1);
		dlc->temp=hash_make_dummy_run();
		dlc->late_use_node=NULL;	
	}

	snode *temp;
	for_each_sk(temp,slc->body){
		run_t *rt=(run_t*)temp->value;
		hash *h=r2h(rt);
		for(int i=0; i<HENTRY; i++){
#ifdef KVSSD
			if(h->b[i].lpa.key==NULL)continue;
#else
			if(h->b[i].lpa==UINT_MAX) continue;
#endif
			hash_insert_into(dlc,h->b[i],des->fpr);
			hash_range_update(des,NULL,h->b[i].lpa);
		}
	}
	((hash_body*)des->level_data)->lev_cache_data=dlc;
}

void hash_cache_free(level *lev){
	hash_body *lc=cfl(lev);
	hash_body_free(lc);
	((hash_body*)lev->level_data)->lev_cache_data=NULL;
}

void hash_cache_move(level *src, level *des){
	hash_body *slc=cfl(src);
	((hash_body*)des->level_data)->lev_cache_data=slc;
	((hash_body*)src->level_data)->lev_cache_data=NULL;
	des->start=src->start;
	des->end=src->end;
}

int hash_cache_comp_formatting(level *lev,run_t *** des){
	hash_body* lc=cfl(lev);
	run_t **res;
	int idx=0;
	if(lc->body==NULL){
		res=(run_t **)malloc(sizeof(run_t*)*(2));
		res[idx++]=lc->temp;
	}
	else{
		res=(run_t **)malloc(sizeof(run_t*)*(lc->body->size+1));
		snode *temp;
		for_each_sk(temp,lc->body){
			res[idx]=(run_t*)temp->value;
			res[idx]->cpt_data->done=true;
			idx++;
		}
	}

	res[idx]=NULL;
	*des=res;
	return idx;
}

keyset *hash_cache_find(level *lev , KEYT lpa){
	hash_body *lc=cfl(lev);
#ifdef KVSSD
	if(KEYCMP(lev->start,lpa)>0 || KEYCMP(lev->end,lpa)<0)
#else
	if(lev->start>lpa || lev->end<lpa) 
#endif
	{
		return NULL;
	}
	if(lc->temp){
		run_t *t=(run_t *)lc->temp;
		keyset *a=hash_find_keyset((char*)t->cpt_data->sets, lpa);
		if(a) return a;
	}
	if(!lc->body) return NULL;
	snode *temp=skiplist_strict_range_search(lc->body,lpa);
	run_t *r=(run_t*)temp->value;
	return hash_find_keyset((char*)r->cpt_data->sets,lpa);
}
run_t *hash_cache_find_run(level *lev,KEYT lpa){
	hash_body *lc=cfl(lev);
#ifdef KVSSD
	if(KEYCMP(lev->start,lpa)>0 || KEYCMP(lev->end,lpa)<0) return NULL;
#else
	if(lev->start>lpa || lev->end<lpa) return NULL;,
#endif
	if(lc->temp){
		run_t *t=(run_t *)lc->temp;
		return t;
	}
	if(!lc->body) return NULL;
	snode *temp=skiplist_strict_range_search(lc->body,lpa);
	run_t *r=(run_t*)temp->value;
	return r;
}

int hash_cache_get_sz(level* lev){
	hash_body *lc=cfl(lev);
	if(!lc) return 0;
	if(lc->temp) return 1;
	return lc->body->size;
}

run_t *hash_range_find_start(level *lev, KEYT start){
	hash_body *hb=(hash_body*)lev->level_data;
	skiplist *body=hb->body;
	int res=0;
	snode *temp=skiplist_strict_range_search(body,start);
	run_t *ptr;
	while(temp && temp!=body->header){
		ptr=(run_t*)temp->value;
#ifdef KVSSD
		if(KEYCMP(ptr->key,start)>0)
#else
		if(ptr->key > start)
#endif
		{
			return ptr;
		}
		temp=temp->list[1];
	}
	return NULL;
}
#endif
