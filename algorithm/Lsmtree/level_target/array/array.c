#include "array.h"
#include "../../level.h"
#include "../../bloomfilter.h"
#include "../../lsmtree.h"
#include "../../../../interface/interface.h"
#include "../../../../include/utils/kvssd.h"
#include "../../nocpy.h"
extern KEYT key_max, key_min;
extern lsmtree LSM;
level_ops a_ops={
	.init=array_init,
	.release=array_free,
	.insert=array_insert,
	.lev_copy=array_lev_copy,
	.find_keyset=array_find_keyset,
	.find_idx_lower_bound=array_find_idx_lower_bound,
	.find_keyset_first=array_find_keyset_first,
	.find_keyset_last=array_find_keyset_last,
	.full_check=def_fchk,
	.tier_align=array_tier_align,
	.move_heap=def_move_heap,
	.chk_overlap=array_chk_overlap,
	.range_find=array_range_find,
	.range_find_compaction=array_range_find_compaction,
	.unmatch_find=array_unmatch_find,
	//.range_find_lowerbound=array_range_find_lowerbound,
	.next_run=array_next_run,
	//.range_find_nxt_node=NULL,
	.get_iter=array_get_iter,
	.get_iter_from_run=array_get_iter_from_run,
	.iter_nxt=array_iter_nxt,
	.get_number_runs=array_get_numbers_run,
	.get_max_table_entry=a_max_table_entry,
	.get_max_flush_entry=a_max_flush_entry,

	.keyset_iter_init=array_key_iter_init,
	.keyset_iter_nxt=array_key_iter_nxt,

	.mem_cvt2table=array_mem_cvt2table,

	.merger=array_pipe_merger,
	.cutter=array_pipe_cutter,
	.partial_merger_cutter=array_pipe_p_merger_cutter,
	.normal_merger=array_normal_merger,
//	.normal_cutter=array_multi_cutter,
#ifdef BLOOM
	.making_filter=array_making_filter,
#endif
	.get_run_idx=array_get_run_idx,
	.make_run=array_make_run,
	.find_run=array_find_run,
	.find_run_num=array_find_run_num,
	.release_run=array_free_run,
	.run_cpy=array_run_cpy,

	.moveTo_fr_page=def_moveTo_fr_page,
	.get_page=def_get_page,
	.block_fchk=def_blk_fchk,
	.range_update=array_range_update,
	.cache_comp_formatting=array_cache_comp_formatting,
/*
	.cache_insert=array_cache_insert,
	.cache_merge=array_cache_merge,
	.cache_free=array_cache_free,
	.cache_move=array_cache_move,
	.cache_find=array_cache_find,
	.cache_find_run_data=array_cache_find_run_data,
	.cache_next_run_data=array_cache_next_run_data,
	//.cache_get_body=array_cache_get_body,
	.cache_get_iter=array_cache_get_iter,
	.cache_iter_nxt=array_cache_iter_nxt,
*/	
	.header_get_keyiter=array_header_get_keyiter,
	.header_next_key=array_header_next_key,
	.header_next_key_pick=array_header_next_key_pick,
#ifdef KVSSD
	.get_lpa_from_data=array_get_lpa_from_data,
#endif
	.get_level_mem_size=array_get_level_mem_size,
	.checking_each_key=array_checking_each_key,
	.check_order=array_check_order,
	.print=array_print,
	.print_run=array_print_run,
	.print_level_summary=array_print_level_summary,
	.all_print=array_all_print,
	.header_print=array_header_print
};

void array_range_update(level *lev,run_t* r, KEYT key){
#ifdef KVSSD
	if(KEYCMP(lev->start,key)>0) lev->start=key;
	if(KEYCMP(lev->end,key)<0) lev->end=key;
#else
	if(lev->start>key) lev->start=key;
	if(lev->end<key) lev->end=key;
#endif
};

level* array_init(int size, int idx, float fpr, bool istier){
	level *res=(level*)calloc(sizeof(level),1);
	array_body *b=(array_body*)calloc(sizeof(array_body),1);

	//b->skip=NULL;
	
	b->arrs=(run_t*)calloc(sizeof(run_t),size);
	/*
	if(idx<LSM.LEVELCACHING){
		b->skip=skiplist_init();
	}*/

	res->idx=idx;
	res->fpr=fpr;
	res->istier=istier;
	res->m_num=size;
	res->n_num=0;
#ifdef KVSSD
	res->start=key_max;
	res->end=key_min;
#else
	res->start=UINT_MAX;
	res->end=0;
#endif
	res->level_data=(void*)b;

#ifdef BLOOM
	res->filter=bf_init(LSM.keynum_in_header*size,fpr);
#endif
	//res->h=llog_init();

	return res;
}

void array_free(level* lev){
	array_body *b=(array_body*)lev->level_data;
	array_body_free(b->arrs,lev->n_num);
/*
	if(lev->h){
		llog_free(lev->h);
	}
*/
	//printf("skip->free\n");
	/*
	if(lev->idx<LSM.LEVELCACHING){
		skiplist_free(b->skip);
	}*/
#ifdef BLOOM
	if(lev->filter)
		bf_free(lev->filter);
#endif
	free(b);
	free(lev);
}

void array_run_cpy_to(run_t *input, run_t *res){
	//memset(res,0,sizeof(run_t));
	kvssd_cpy_key(&res->key,&input->key);
	kvssd_cpy_key(&res->end,&input->end);

	res->pbn=input->pbn;
	pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
	if(input->c_entry){
		if(LSM.nocpy){
			res->cache_nocpy_data_ptr=input->cache_nocpy_data_ptr;
			input->cache_nocpy_data_ptr=NULL;
		}
		else{
			res->cache_data=input->cache_data;
			input->cache_data=NULL;
		}
		res->c_entry=input->c_entry;
		res->c_entry->entry=res;
		input->c_entry=NULL;
	}else{
		res->c_entry=NULL;
		if(LSM.nocpy) res->cache_nocpy_data_ptr=NULL;
		else res->cache_data=NULL;
	}
	pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
	if(input->level_caching_data){
		res->level_caching_data=input->level_caching_data;
		input->level_caching_data=NULL;
	}
}

void array_body_free(run_t *runs, int size){
	for(int i=0; i<size; i++){
		array_free_run(&runs[i]);
	}
	free(runs);
}

run_t* array_insert(level *lev, run_t* r){
	if(lev->m_num<=lev->n_num){
	//	array_print(lev);
		printf("level full!!!!\n");
		abort();
	}

	array_body *b=(array_body*)lev->level_data;
	//if(lev->n_num==0){
	//}
	/*
	if(b->arrs==NULL){
		b->arrs=(run_t*)malloc(sizeof(run_t)*lev->m_num);
	}*/
	run_t *arrs=b->arrs;
	run_t *target=&arrs[lev->n_num];
	array_run_cpy_to(r,target);

	if(lev->idx>=LSM.LEVELCACHING && LSM.comp_opt!=HW && !target->c_entry && r->cpt_data && cache_insertable(LSM.lsm_cache)){
		if(lev->idx>=LSM.LEVELCACHING && LSM.hw_read){

		}
		else{
			if(LSM.nocpy)
				target->cache_nocpy_data_ptr=nocpy_pick(r->pbn);
			else{
				target->cache_data=htable_copy(r->cpt_data);
				r->cpt_data->sets=NULL;
			}
			target->c_entry=cache_insert(LSM.lsm_cache,target,0);
		}
	}

	array_range_update(lev,NULL,target->key);
	array_range_update(lev,NULL,target->end);

	lev->n_num++;
	return target;
}

keyset* array_find_keyset(char *data,KEYT lpa){
	char *body=data;
	uint16_t *bitmap=(uint16_t*)body;
	int s=1,e=bitmap[0];
	KEYT target;
	while(s<=e){
		int mid=(s+e)/2;
		target.key=&body[bitmap[mid]+sizeof(ppa_t)];
		target.len=bitmap[mid+1]-bitmap[mid]-sizeof(ppa_t);
		int res=KEYCMP(target,lpa);
		if(res==0){
			return (keyset*)&body[bitmap[mid]];
		}
		else if(res<0){
			s=mid+1;
		}
		else{
			e=mid-1;
		}
	}
	return NULL;
}

run_t *array_find_run( level* lev,KEYT lpa){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	if(!arrs || lev->n_num==0) return NULL;
	int end=lev->n_num-1;
	int start=0;
	int mid;

	int res1; //1:compare with start, 2:compare with end
	mid=(start+end)/2;
	while(1){
		res1=KEYCMP(arrs[mid].key,lpa);
		if(res1>0) end=mid-1;
		else if(res1<0) start=mid+1;
		else {
			return &arrs[mid];
		} 
		mid=(start+end)/2;
	//	__builtin_prefetch(&arrs[(mid+1+end)/2].key,0,1);
	//	__builtin_prefetch(&arrs[(start+mid-1)/2].key,0,1);
		if(start>end){
			return &arrs[mid];
		}   
	}
	return NULL;
}

run_t **array_find_run_num( level* lev,KEYT lpa, uint32_t num){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	if(!arrs || lev->n_num==0) return NULL;
#ifdef KVSSD
	if(KEYCMP(lev->start,lpa)>0 || KEYCMP(lev->end,lpa)<0) return NULL;
#else
	if(lev->start>lpa || lev->end<lpa) return NULL;
#endif
	if(lev->istier) return (run_t**)-1;

	int target_idx=array_binary_search(arrs,lev->n_num,lpa);
	if(target_idx==-1) return NULL;
	run_t **res=(run_t**)calloc(sizeof(run_t*),num+1);
	uint32_t idx;
	for(idx=0; idx<num; idx++){
		if(target_idx<lev->n_num){
			res[idx]=&arrs[target_idx++];
		}else{
			break;
		}
	}
	res[idx]=NULL;
	return res;
}

uint32_t array_range_find( level *lev ,KEYT s, KEYT e,  run_t ***rc){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	int res=0;
	run_t *ptr;
	run_t **r=(run_t**)malloc(sizeof(run_t*)*(lev->n_num+1));
	int target_idx=array_binary_search(arrs,lev->n_num,s);
	if(target_idx==-1) target_idx=0;
	for(int i=target_idx;i<lev->n_num; i++){
		ptr=(run_t*)&arrs[i];
#ifdef KVSSD
		if(!(KEYCMP(ptr->end,s)<0 || KEYCMP(ptr->key,e)>0))
#else
		if(!(ptr->end<s || ptr->key>e))
#endif
		{
			r[res++]=ptr;
		}
#ifdef KVSSD
		else if(KEYCMP(e,ptr->key)<0)
#else
		else if(e< ptr->key)
#endif
		{
			break;
		}
	}
	r[res]=NULL;
	*rc=r;
	return res;
}

uint32_t array_range_find_compaction( level *lev ,KEYT s, KEYT e,  run_t ***rc){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	int res=0;
	run_t *ptr;
	run_t **r=(run_t**)malloc(sizeof(run_t*)*(lev->n_num+1));
	//int target_idx=array_binary_search(arrs,lev->n_num,s);
	int target_idx=array_bound_search(arrs,lev->n_num,s,true);
	if(target_idx==-1) target_idx=0;
	for(int i=target_idx;i<lev->n_num; i++){
		ptr=(run_t*)&arrs[i];
		r[res++]=ptr;
	}
	r[res]=NULL;
	*rc=r;
	return res;
}

uint32_t array_unmatch_find( level *lev,KEYT s, KEYT e,  run_t ***rc){
	array_body *b=(array_body*)lev->level_data;
	run_t *arrs=b->arrs;
	int res=0;
	run_t *ptr;
	run_t **r=(run_t**)malloc(sizeof(run_t*)*(lev->n_num+1));
	for(int i=0; i!=-1 && i<lev->n_num; i++){
		ptr=(run_t*)&arrs[i];
#ifdef KVSSD
		if((KEYCMP(ptr->end,s)<0))
#else
		if(!(ptr->end<s || ptr->key>e))
#endif
		{
			r[res++]=ptr;
		}
	}

	r[res]=NULL;
	*rc=r;
	return res;
}

void array_free_run(run_t *e){
	//static int cnt=0;
	pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
	if(e->c_entry){
		if(LSM.nocpy) e->cache_nocpy_data_ptr=NULL;
		else htable_free(e->cache_data);
		cache_delete_entry_only(LSM.lsm_cache,e);
	}
	pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
	free(e->level_caching_data);
	free(e->key.key);
	free(e->end.key);
}
run_t * array_run_cpy( run_t *input){
	run_t *res=(run_t*)calloc(sizeof(run_t),1);
	kvssd_cpy_key(&res->key,&input->key);
	kvssd_cpy_key(&res->end,&input->end);
	res->pbn=input->pbn;

	pthread_mutex_lock(&LSM.lsm_cache->cache_lock);
	if(input->c_entry){
		if(LSM.nocpy){
			res->cache_nocpy_data_ptr=input->cache_nocpy_data_ptr;
			input->cache_nocpy_data_ptr=NULL;
		}
		else{
			res->cache_data=input->cache_data;
			input->cache_data=NULL;
		}
		res->c_entry=input->c_entry;
		res->c_entry->entry=res;
		input->c_entry=NULL;
	}else{
		res->c_entry=NULL;
		if(LSM.nocpy) res->cache_nocpy_data_ptr=NULL;
		else res->cache_data=NULL;
	}
	pthread_mutex_unlock(&LSM.lsm_cache->cache_lock);
	/*res->isflying=0;
	res->run_data=NULL;
	res->req=NULL;

	res->cpt_data=NULL;
	res->iscompactioning=false;
	res->wait_idx=0;*/
	return res;
}

lev_iter* array_get_iter( level *lev,KEYT start, KEYT end){
	/*
	if(lev->idx<LSM.LEVELCACHING){
		return array_cache_get_iter(lev, start, end);
	}*/
	array_body *b=(array_body*)lev->level_data;
	lev_iter *it=(lev_iter*)malloc(sizeof(lev_iter));
	it->from=start;
	it->to=end;
	a_iter *iter=(a_iter*)malloc(sizeof(a_iter));

	if(KEYCMP(start,lev->start)==0 && KEYCMP(end,lev->end)==0){
		iter->ispartial=false;	
		iter->max=lev->n_num;
		iter->now=0;
	}
	else{
	//	printf("should do somthing!\n");
		iter->now=array_bound_search(b->arrs,lev->n_num,start,true);
		iter->max=array_bound_search(b->arrs,lev->n_num,end,true);
		iter->ispartial=true;
	}
	iter->arrs=b->arrs;

	it->iter_data=(void*)iter;
	it->lev_idx=lev->idx;
	return it;
}

lev_iter *array_get_iter_from_run(level *lev, run_t *sr, run_t *er){
	array_body *b=(array_body*)lev->level_data;
	lev_iter *it=(lev_iter*)malloc(sizeof(lev_iter));
	a_iter *iter=(a_iter*)malloc(sizeof(a_iter));

	iter->now=(sr - b->arrs);
	iter->max=lev->n_num;
	iter->arrs=b->arrs;
	it->iter_data=(void*)iter;
	it->lev_idx=lev->idx;
	return it;
}


run_t * array_iter_nxt( lev_iter* in){
	a_iter *iter=(a_iter*)in->iter_data;
	if(iter->now==iter->max){
		free(iter);
		free(in);
		return NULL;
	}else{
		if(iter->ispartial){
			return &iter->arrs[iter->now++];
		}else{
			return &iter->arrs[iter->now++];
		}
	}
	return NULL;
}

void array_print(level *lev){
	array_body *b=(array_body*)lev->level_data;
	if(lev->idx<LSM.LEVELCACHING){
		/*
		if(!b->skip || b->skip->size==0){
			printf("[caching data] empty\n");	
		}else{*/
			//printf("[caching data] # of entry:%lu -> run:%d\n",b->skip->size,array_get_numbers_run(lev));
//		}
		return;
	}
	run_t *arrs=b->arrs;
	for(int i=0; i<lev->n_num;i++){
		run_t *rtemp=&arrs[i];
#ifdef KVSSD
		printf("[%d]%.*s~%.*s(%u)-ptr:%p cached:%s wait:%d iscomp:%d\n",i,KEYFORMAT(rtemp->key),KEYFORMAT(rtemp->end),rtemp->pbn,rtemp,rtemp->c_entry?"true":"false",rtemp->wait_idx,rtemp->iscompactioning);
#else
		printf("[%d]%d~%d(%d)-ptr:%p cached:%s wait:%d\n",i,rtemp->key,rtemp->end,rtemp->pbn,rtemp,rtemp->c_entry?"true":"false",rtemp->wait_idx);,
#endif
	}
}


void array_all_print(){
	uint32_t res=0;
	for(int i=0; i<LSM.LEVELN; i++){
		printf("[LEVEL : %d]\n",i);
		array_print(LSM.disk[i]);
		printf("\n");
		res+=array_get_level_mem_size(LSM.disk[i]);
	}
	printf("all level mem size :%dMB\n",res/M);
}

uint32_t a_max_table_entry(){
#ifdef KVSSD
	return 1;
#else
	return 1024;
#endif
}
uint32_t a_max_flush_entry(uint32_t in){
	return in;
}

int array_binary_search(run_t *body,uint32_t max_t, KEYT lpa){
	int start=0;
	int end=max_t-1;
	int mid;

	int res1, res2; //1:compare with start, 2:compare with end
	while(start==end ||start<end){
		mid=(start+end)/2;
		res1=KEYCMP(body[mid].key,lpa);
		res2=KEYCMP(body[mid].end,lpa);
		if(res1<=0 && res2>=0)
			return mid;
		if(res1>0) end=mid-1;
		else if(res2<0) start=mid+1;
	}
	return -1;
}

//int array_lowerbound_search(run_t *body, uint32_t max_t, KEYT lpa){
int array_bound_search(run_t *body, uint32_t max_t, KEYT lpa, bool islower){
	int start=0;
	int end=max_t-1;
	int mid=0;

	int res1=0, res2=0; //1:compare with start, 2:compare with end
	while(start==end ||start<end){
		mid=(start+end)/2;
		res1=KEYCMP(body[mid].key,lpa);
		res2=KEYCMP(body[mid].end,lpa);
		if(res1<=0 && res2>=0){
			if(islower)return mid;
			else return mid+1;
		}
		if(res1>0) end=mid-1;
		else if(res2<0) start=mid+1;
	}
	
	if(res1>0) return mid;
	else if (res2<0 && mid < (int)max_t-1) return mid+1;
	else return -1;
}

run_t *array_make_run(KEYT start, KEYT end, uint32_t pbn){
	run_t * res=(run_t*)calloc(sizeof(run_t),1);
	kvssd_cpy_key(&res->key,&start);
	kvssd_cpy_key(&res->end,&end);
	res->pbn=pbn;
	res->run_data=NULL;
	res->c_entry=NULL;
	res->wait_idx=0;
	return res;
}

KEYT *array_get_lpa_from_data(char *data,ppa_t ppa,bool isheader){
	KEYT *res=(KEYT*)malloc(sizeof(KEYT));
	
	if(isheader){
		int idx;
		KEYT key;
		ppa_t *ppa;
		uint16_t *bitmap;
		char *body=data;
		bitmap=(uint16_t*)body;

		for_each_header_start(idx,key,ppa,bitmap,body)
			kvssd_cpy_key(res,&key);
			return res;
		for_each_header_end
	}
	else{
#ifdef EMULATOR
		KEYT *t=lsm_simul_get(ppa);
		res->len=t->len;
		res->key=t->key;
#else
		res->len=*(uint8_t*)data;
		res->key=&data[sizeof(uint8_t)];
#endif
	}
	return res;
}

keyset_iter *array_key_iter_init(char *key_data, int start){
	keyset_iter *res=(keyset_iter*)malloc(sizeof(keyset_iter));
	a_key_iter *data=(a_key_iter*)malloc(sizeof(a_key_iter));

	res->private_data=(void*)data;
	data->idx=start;
	data->body=key_data;
	data->bitmap=(uint16_t*)data->body;
	return res;
}

keyset *array_key_iter_nxt(keyset_iter *k_iter, keyset *target){
	a_key_iter *ds=(a_key_iter*)k_iter->private_data;
	if(ds->bitmap[ds->idx]==UINT16_MAX || ds->idx>ds->bitmap[0]){
		free(ds);
		free(k_iter);
		return NULL;
	}
	ds->ppa=(uint32_t*)&ds->body[ds->bitmap[ds->idx]];
	ds->key.key=(char*)&ds->body[ds->bitmap[ds->idx]+sizeof(uint32_t)];
	ds->key.len=ds->bitmap[ds->idx+1]-ds->bitmap[ds->idx]-sizeof(uint32_t);

	target->lpa=ds->key;
	target->ppa=*ds->ppa;
	ds->idx++;
	return target;
}

void array_find_keyset_first(char *data, KEYT *des){
	char *body=data;
	uint16_t *bitmap=(uint16_t *)body;
	
	des->key=&body[bitmap[1]+sizeof(uint32_t)];
	des->len=bitmap[1]-bitmap[2]-sizeof(uint32_t);
}

void array_find_keyset_last(char *data, KEYT *des){
	char *body=data;
	uint16_t *bitmap=(uint16_t *)body;
	int e=bitmap[0];
	des->key=&body[bitmap[e]+sizeof(uint32_t)];
	des->len=bitmap[e]-bitmap[e+1]-sizeof(uint32_t);
}

uint32_t array_find_idx_lower_bound(char *data, KEYT lpa){
	char *body=data;
	uint16_t *bitmap=(uint16_t*)body;
	int s=1, e=bitmap[0];
	int mid=0,res=0;
	KEYT target;
	while(s<=e){
		mid=(s+e)/2;
		target.key=&body[bitmap[mid]+sizeof(uint32_t)];
		target.len=bitmap[mid+1]-bitmap[mid]-sizeof(uint32_t);
		res=KEYCMP(target,lpa);
		if(res==0){
			return mid;
		}
		else if(res<0){
			s=mid+1;
		}
		else{
			e=mid-1;
		}
	}

	if(res<0){
		//lpa is bigger
		return mid+1;
	}
	else{
		//lpa is smaller
		return mid;
	}
}

run_t *array_get_run_idx(level *lev, int idx){
	array_body *b=(array_body*)lev->level_data;
	return &b->arrs[idx];
}

uint32_t array_get_level_mem_size(level *lev){
	uint32_t res=0;
	res+=sizeof(level)+sizeof(run_t)*lev->m_num;
	return res;
}

void array_print_level_summary(){
	for(int i=0; i<LSM.LEVELN; i++){
		if(LSM.disk[i]->n_num==0){
			printf("[%d - %s ] n_num:%d m_num:%d\n",i+1,i<LSM.LEVELCACHING?"C":"NC",LSM.disk[i]->n_num,LSM.disk[i]->m_num);
		}
		else {
#ifdef BLOOM
			printf("[%d - %s (%.*s ~ %.*s)] n_num:%d m_num:%d filter:%p\n",i+1,i<LSM.LEVELCACHING?"C":"NC",KEYFORMAT(LSM.disk[i]->start),KEYFORMAT(LSM.disk[i]->end),LSM.disk[i]->n_num,LSM.disk[i]->m_num,LSM.disk[i]->filter);
#else
			printf("[%d - %s (%.*s ~ %.*s)] n_num:%d m_num:%d %.*s ~ %.*s\n",i+1,i<LSM.LEVELCACHING?"C":"NC",KEYFORMAT(LSM.disk[i]->start),KEYFORMAT(LSM.disk[i]->end),LSM.disk[i]->n_num,LSM.disk[i]->m_num,KEYFORMAT(LSM.disk[i]->start),KEYFORMAT(LSM.disk[i]->end));
#endif
		}
	}	
}

uint32_t array_get_numbers_run(level *lev){
	return lev->n_num;
}

void array_check_order(level *lev){
	if(lev->idx<LSM.LEVELCACHING || lev->n_num==0) return;
	run_t *bef=array_get_run_idx(lev,0);
	for(int i=1; i<lev->n_num; i++){
		run_t *now=array_get_run_idx(lev,i);
		if(KEYCMP(bef->end,now->key)>=0){
			abort();
		}
		bef=now;
	}
}

void array_print_run(run_t * r){
	printf("%.*s ~ %.*s : %d\n",KEYFORMAT(r->key),KEYFORMAT(r->end),r->pbn);
}

void array_lev_copy(level *des, level *src){
	kvssd_cpy_key(&des->start,&src->start);
	kvssd_cpy_key(&des->end,&src->end);
	des->n_num=src->n_num;
#ifdef BLOOM
	des->filter=src->filter;
#endif
	
	array_body *db=(array_body*)des->level_data;
	array_body *sb=(array_body*)src->level_data;

	for(int i=0; i<src->n_num; i++){
		array_run_cpy_to(&sb->arrs[i],&db->arrs[i]);
	}
}
