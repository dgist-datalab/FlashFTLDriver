#include "lsmtree_iter.h"
#include "../../include/lsm_settings.h"
#include "../../include/utils/thpool.h"
#include "../../include/utils/kvssd.h"
#include "skiplist.h"
#include "nocpy.h"
iter_master im;
extern lsmtree LSM;
static threadpool multi_handler;
#define M_HANDLER_NUM 1
void* lsm_iter_end_req(algo_req *const req);
void lsm_iter_global_init(){
	im.rb=rb_create();
	int *temp;
	q_init(&im.q,MAXITER);

	for(int i=0; i<MAXITER; i++){
		temp=(int*)malloc(sizeof(int));
		*temp=i;
		q_enqueue((void*)temp,im.q);
	}

	multi_handler=thpool_init(M_HANDLER_NUM);
}

void lsm_make_iterator(lsm_iter *iter, KEYT min){
	skiplist *skip=skiplist_init();
	for(int i=LEVELN-1; i>=0; i--){
		for(int j=0; iter->datas[i][j]!=NULL; j++){
			int start_idx=LSM.lop->find_idx_lower_bound((char*)iter->datas[i][j],*iter->target_key);
#ifdef KVSSD
			keyset_iter *k_iter=LSM.lop->keyset_iter_init((char*)iter->datas[i][j],start_idx);
			keyset *key=(keyset*)malloc(sizeof(keyset));
			for(key=LSM.lop->keyset_iter_nxt(k_iter,key); key!=NULL; key=LSM.lop->keyset_iter_nxt(k_iter, key)){
				skiplist_insert_iter(skip,key->lpa,key->ppa);
			}
			free(key);
#else
			for(int j=1; j<1024; j++){
				keyset *key=&((keyset*)(iter->datas[i]))[j];
				if(key->lpa==UINT_MAX)continue;
				if(key->lpa<min){
					printf("lpa:%u\n",key->lpa);
					continue;
				}
				skiplist_insert_iter(skip,key->lpa,key->ppa);
			}
#endif
		}
	}

	/*memtable*/
	snode *from=skiplist_find_lowerbound(LSM.memtable,*iter->target_key);
	snode *to=skiplist_find_lowerbound(LSM.memtable,*iter->last_key);
	snode *temp;
	for(temp=from; temp!=to; temp=temp->list[1]){
		skiplist_insert_iter(skip,temp->key,UINT_MAX);
	}

	iter->max_idx=skip->size;
	iter->body=skip;
	fdriver_unlock(&iter->initiated_lock);
}


bool checking_multi_handler_start(int a, int b){
	if(a!=b)return 1;
	else return 0;
}

bool release_multi_handler_start(int a, int b){
	if(a==b)return 1;
	else return 0;
}

void lsm_multi_handler(void *arg, int id){
	request *req=(request*)arg;
	lsmtree_iter_req_param *req_param=(lsmtree_iter_req_param*)req->params;
	lsm_iter *iter=req_param->iter;
	switch(req->type){
		case FS_ITER_NXT_VALUE_T:
		case FS_ITER_NXT_T:
		case FS_ITER_CRT_T:
			cl_grep_with_f(iter->conditional_lock,iter->target,iter->received,checking_multi_handler_start);
			lsm_make_iterator(req_param->iter,req->key);
			req->ppa=*req_param->iter->iter_idx;
			/*
			if(req->type!=FS_ITER_NXT_T){
				fdriver_unlock(&req_param->iter->initiated_lock);
			}
			   should free in iter release
			for(i=0; i<LEVELN; i++){
				inf_free_valueset(req->multi_value[i],FS_MALLOC_R);
			}
			 */	
			req->end_req(req);
			break;
		case FS_ITER_ALL_T:
		case FS_ITER_ALL_VALUE_T:
			cl_grep_with_f(iter->conditional_lock,iter->target,iter->received,checking_multi_handler_start);
			lsm_make_iterator(req_param->iter,req->key);
			req->ppa=*req_param->iter->iter_idx;
			break;
	}

	if(req->type==FS_ITER_NXT_T){
		fdriver_unlock(&req_param->iter->initiated_lock);
	}
	fdriver_unlock(&req_param->iter->use_lock);
	free(req_param);
}

void *lsm_iter_end_req(algo_req *const req){
	lsmtree_iter_param *param=(lsmtree_iter_param*)req->params;
	request *parents=req->parents;
	lsmtree_iter_req_param *req_param=(lsmtree_iter_req_param*)parents->params;
	lsm_iter *iter=req_param->iter;

//	bool multi_handler_start=false;
	switch(param->lsm_type){
		case DATAR:
			req_param->value_received++;
			if(req_param->value_received==req_param->value_target){
				req->end_req(req);
				free(req_param);
			}
			break;
		case HEADERR:
			if(LSM.nocpy) iter->datas[param->level][param->idx]=nocpy_pick(param->ppa);
			else{
				iter->datas[param->level][param->idx]=(char*)malloc(PAGESIZE);
				memcpy(iter->datas[param->level][param->idx],param->value->vaue,PAGESIZE);
			}
			cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
			inf_free_valueset(param->value,FS_MALLOC_R);

			iter->received++;

			if(!iter->multi_handler_start_check){
				iter->multi_handler_start_check=true;

	//			multi_handler_start=true;
			}
			break;
	}

	free(param);
	return NULL;
}

algo_req* lsm_iter_req_factory(request *req, lsmtree_iter_param *param,uint8_t type){
	algo_req *lsm_req=(algo_req*)malloc(sizeof(algo_req));
	param->lsm_type=type;
	lsm_req->params=param;
	lsm_req->parents=req;
	lsm_req->end_req=lsm_iter_end_req;
	lsm_req->type_lower=0;
	lsm_req->rapid=true;
	lsm_req->type=type;
	return lsm_req;
}

void lsm_iter_shot_readreq(run_t *r,request *req, int level, int idx){
	lsmtree_iter_param *param;
	algo_req *lsm_req;
	param=(lsmtree_iter_param*)malloc(sizeof(lsmtree_iter_param));
	param->level=level;
	param->idx=idx;
	param->ppa=r->pbn;
	param->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
	lsm_req=lsm_iter_req_factory(req,param,HEADERR);
	LSM.li->read(param->ppa,PAGESIZE,param->value,ASYNC,lsm_req);
}

void lsm_iter_read_header(lsm_iter *iter, lsmtree_iter_req_param *req_param, request *req,bool ismore){
	KEYT *end=(KEYT*)malloc(sizeof(KEYT));
	kvssd_cpy_key(end,&req->key);
	end->key[end->len-1]+=1;
	iter->last_key=end;

	char *target_data;
	iter->target=iter->received=0;
	for(int i=0; i<LEVELCACHING; i++){
		iter->datas[i]=(char**)malloc(sizeof(char*)*(LSM.disk[i]->m_num+1));
		int idx=0;
		lev_iter *t_iter=LSM.lop->cache_get_iter(LSM.disk[i],req->key,*end);
		while((target_data=LSM.lop->cache_iter_nxt(t_iter))!=NULL){
			//lsm_iter_shot_readreq(target_run,req,i,idx);
			iter->datas[i][idx]=target_data;
			idx++;
		}
		iter->datas[i][idx]=NULL;
	}
	
	run_t *target_run;
	for(int i=LEVELCACHING; i<LEVELN; i++){
		iter->datas[i]=(char**)malloc(sizeof(char*)*(LSM.disk[i]->m_num+1));
		int idx=0;
		lev_iter *t_iter=LSM.lop->get_iter(LSM.disk[i],req->key,*end);
		while((target_run=LSM.lop->iter_nxt(t_iter))!=NULL){
			iter->target++;
			lsm_iter_shot_readreq(target_run,req,i,idx);
			idx++;
		}
		iter->datas[i][idx]=NULL;
	}

	while(thpool_num_threads_working(multi_handler)>=M_HANDLER_NUM);
	thpool_add_work(multi_handler,lsm_multi_handler,(void*)req);
}

uint32_t lsm_iter_create(request *req){
	/*should make req have many values*/
	static bool first_create=false;
	printf("req-key:%.*s\n",KEYFORMAT(req->key));
	if(!first_create){
		lsm_iter_global_init();
		first_create=true;
	}
	lsm_iter *new_iter;
	lsmtree_iter_req_param *req_param;
	LSM.lop->all_print();
	if(!req->params){
		new_iter=(lsm_iter*)malloc(sizeof(lsm_iter));
		void *iter_idx;
		while(!(iter_idx=q_dequeue(im.q))){}
		new_iter->multi_handler_start_check=false;
		new_iter->iter_idx=(int*)iter_idx;
		new_iter->conditional_lock=cl_init(LEVELN,true);
		new_iter->received=0;
		new_iter->target=LEVELN;

		req->ppa=*new_iter->iter_idx;
		rb_insert_int(im.rb,*new_iter->iter_idx,(void*)new_iter);
		new_iter->target_key=&req->key;
		new_iter->datas=(char***)malloc(sizeof(char**)*LEVELN);
		new_iter->now_idx=new_iter->max_idx=0;
		new_iter->last_node=NULL;
		
		fdriver_lock_init(&new_iter->initiated_lock,0);

		memset(new_iter->datas,0,sizeof(char**)*LEVELN);

		req_param=(lsmtree_iter_req_param*)malloc(sizeof(lsmtree_iter_req_param));
		req_param->now_level=0;

		req_param->iter=new_iter;
		req->params=(void*)req_param;
		fdriver_lock_init(&new_iter->use_lock,1);
	}else{
		req_param=(lsmtree_iter_req_param*)req->params;
		new_iter=req_param->iter;
	}

	fdriver_try_lock(&new_iter->use_lock);
	lsm_iter_read_header(new_iter,req_param,req,false);
	return 1;
}

uint32_t lsm_iter_next(request *req){
	/*find target node*/
	Redblack t_rb;
	rb_find_int(im.rb,req->ppa,&t_rb);
	lsm_iter *iter=(lsm_iter*)t_rb->item;
	fdriver_lock(&iter->initiated_lock);
	fdriver_try_lock(&iter->use_lock);

	//int len;
	//len=req->num;
	//value_set *value=req->value;

	snode *from=iter->last_node;
	if(from==NULL)
		from=iter->body->header->list[1];

	char *res;
	int res_size=iter->body->all_length+iter->body->size;
	res=(char*)malloc(res_size+1);

	int idx=0;
	snode *temp;
	for_each_sk_from(temp,from,iter->body){
		/*make header*/
		memcpy(&res[idx],temp->key.key,temp->key.len);
		idx+=temp->key.len;
		res[idx]=0;
		idx++;
		//if(idx+temp->list[1]->key.len+1>PAGESIZE) break;
	}
	//iter->last_node=temp->list[1];
	res[idx]=0;
	*req->app_result=res;
	fdriver_unlock(&iter->initiated_lock);
	req->end_req(req);
	fdriver_unlock(&iter->use_lock);
	return 1;	
}

uint32_t lsm_iter_next_with_value(request *req){
	Redblack t_rb;
	rb_find_int(im.rb,req->ppa,&t_rb);
		
	lsm_iter *iter=(lsm_iter*)t_rb->item;

	fdriver_lock(&iter->initiated_lock);
	fdriver_try_lock(&iter->use_lock);
	
	snode *temp;
	snode *from=iter->last_node;
	
	req->multi_value=(value_set**)malloc(sizeof(value_set*)*iter->max_idx);
	lsmtree_iter_req_param* req_param=(lsmtree_iter_req_param*)malloc(sizeof(lsmtree_iter_req_param));
	req_param->value_target=iter->max_idx;
	req_param->value_received=0;
	req->params=(void*)req_param;

	algo_req *lsm_req;
	lsmtree_iter_param *param=(lsmtree_iter_param*)malloc(sizeof(lsmtree_iter_param));
	int idx=0;
	for_each_sk_from(temp, from, iter->body){
		req->multi_value[idx]=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
		lsm_req=lsm_iter_req_factory(req,param,DATAR);
		LSM.li->read(temp->ppa,PAGESIZE,param->value,ASYNC,lsm_req);
		idx++;
		if(idx==iter->max_idx)
			break;
	}
	
	fdriver_unlock(&iter->initiated_lock);
	return 1;
}

uint32_t lsm_iter_release(request *req){
	printf("release called\n");
	Redblack target;
	uint32_t iter_id=req->ppa;
	rb_find_int(im.rb,iter_id,&target);
	lsm_iter *temp=(lsm_iter*)target->item;
	fdriver_lock(&temp->use_lock);
	
	for(int i=LEVELN-1; i>=0; i--){
		for(int j=0; temp->datas[i][j]!=NULL; j++){
			free(temp->datas[i][j]);
		}
		free(temp->datas[i]);
	}

	free(temp->datas);
	skiplist_free(temp->body);
	kvssd_free_key(temp->last_key);
	cl_free(temp->conditional_lock);
	rb_delete(target);
	q_enqueue((void*)temp->iter_idx,im.q);
	free(temp);
	req->end_req(req);
	return 1;
}

uint32_t lsm_iter_all_key(request *req){
	lsm_iter_create(req);
	lsm_iter_next(req);
	return 1;
}

uint32_t lsm_iter_all_value(request *req){
	lsm_iter_create(req);
	lsm_iter_next_with_value(req);
	return 1;
}
	/*
	  //////// for lsm_iter_create instead of lsm_iter_read_header///
	run_t *target_run;
	for(int i=0; i<LEVELCACHING; i++){
		pthread_mutex_lock(&LSM.level_lock[i]);
		char *target_data=LSM.lop->cache_find_run_data(LSM.disk[i],req->key);
		pthread_mutex_unlock(&LSM.level_lock[i]);
		if(target_data){
			new_iter->readed_key[i]=(KEYT*)malloc(sizeof(KEYT));
			KEYT src;
			LSM.lop->find_keyset_last(target_data,&src);
			kvssd_cpy_key(new_iter->readed_key[i],&src);
			new_iter->datas[new_iter->datas_idx]=target_data;
			new_iter->datas_idx++;
		}
		else{
			new_iter->readed_key[i]=NULL;
		}
		new_iter->target--;
		cl_release_with_f(new_iter->conditional_lock, new_iter->target,new_iter->received,release_multi_handler_start);
	}

	lsmtree_iter_param * param;
	algo_req *lsm_req;
	for(int i=LEVELCACHING;i<LEVELN; i++){
		pthread_mutex_lock(&LSM.level_lock[i]);
		target_run=LSM.lop->range_find_start(LSM.disk[i],req->key);
		pthread_mutex_unlock(&LSM.level_lock[i]);

		if(target_run){
			new_iter->readed_key[i]=(KEYT*)malloc(sizeof(KEYT));
			kvssd_cpy_key(new_iter->readed_key[i],&target_run->end);
			param=(lsmtree_iter_param*)malloc(sizeof(lsmtree_iter_param));
			param->idx=new_iter->datas_idx;
			param->ppa=target_run->pbn;
			lsm_req=lsm_iter_req_factory(req,param,HEADERR);
			req->multi_value[req_param->now_level]=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			param->value=req->multi_value[req_param->now_level];
			LSM.li->read(target_run->pbn, PAGESIZE,req->multi_value[req_param->now_level],ASYNC,lsm_req);
			req_param->now_level++;
		}
		else{
			new_iter->readed_key[i]=NULL;
			new_iter->target--;
			cl_release_with_f(new_iter->conditional_lock, new_iter->target,new_iter->received,release_multi_handler_start);
		}
		new_iter->datas_idx++;
	}
	*/

/*
   ///////////for lsm_iter_read_more instead of lsm_iter_read_header///////
	run_t *target_run;
	for(int i=0; i<LEVELCACHING; i++){
		if(iter->readed_key[i]==NULL) continue;
		pthread_mutex_lock(&LSM.level_lock[i]);
		char *target_data=LSM.lop->cache_next_run_data(LSM.disk[i],*iter->readed_key[i]);
		pthread_mutex_unlock(&LSM.level_lock[i]);
		if(target_data){
			kvssd_free_key(iter->readed_key[i]);
			KEYT src;
			LSM.lop->find_keyset_last(target_data,&src);
			kvssd_cpy_key(iter->readed_key[i],&src);
			iter->datas[iter->datas_idx]=target_data;
			iter->datas_idx++;
		}
		else{
			iter->readed_key[i]=NULL;
		}
		iter->target--;
		cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
	}

	lsmtree_iter_param * param;
	algo_req *lsm_req;
	
	for(int i=LEVELCACHING;i<LEVELN; i++){
		if(iter->readed_key[i]==NULL) {
			iter->target--;
			cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
			continue;
		}

		pthread_mutex_lock(&LSM.level_lock[i]);
		target_run=LSM.lop->next_run(LSM.disk[i],*iter->readed_key[i]);
		pthread_mutex_unlock(&LSM.level_lock[i]);

		if(target_run){
			kvssd_free_key(iter->readed_key[i]);
			kvssd_cpy_key(iter->readed_key[i],&target_run->end);
			param=(lsmtree_iter_param*)malloc(sizeof(lsmtree_iter_param));
			param->idx=iter->datas_idx;
			param->ppa=target_run->pbn;
			lsm_req=lsm_iter_req_factory(req,param,HEADERR);
			req->multi_value[req_param->now_level]=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			param->value=req->multi_value[req_param->now_level];
			LSM.li->read(target_run->pbn, PAGESIZE,req->multi_value[req_param->now_level],ASYNC,lsm_req);
			req_param->now_level++;
		}
		else{
			iter->readed_key[i]=NULL;
			iter->target--;
			cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
		}
		iter->datas_idx++;
	}*/
/* original data
void lsm_iter_read_header(lsm_iter *iter, lsmtree_iter_req_param *req_param, request *req,bool ismore){
	for(int i=0; i<LEVELCACHING; i++){
		char *target_data;
		if(ismore){
			if(iter->readed_key[i]==NULL) continue;
			pthread_mutex_lock(&LSM.level_lock[i]);
			target_data=LSM.lop->cache_next_run_data(LSM.disk[i],*iter->readed_key[i]);
		}else{
			pthread_mutex_lock(&LSM.level_lock[i]);
			target_data=LSM.lop->cache_find_lowerbound(LSM.disk[i],req->key);
		}
		pthread_mutex_unlock(&LSM.level_lock[i]);
		if(target_data){
			if(iter->readed_key[i])kvssd_free_key(iter->readed_key[i]);
			KEYT src;
			LSM.lop->find_keyset_last(target_data,&src);
			kvssd_cpy_key(iter->readed_key[i],&src);
			iter->datas[iter->datas_idx]=target_data;
			iter->datas_idx++;
		}
		else{
			if(iter->readed_key[i]) kvssd_free_key(iter->readed_key[i]);
			iter->readed_key[i]=NULL;
		}
		iter->target--;
		cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
	}

	lsmtree_iter_param * param;
	algo_req *lsm_req;
	run_t *target_run;
	for(int i=LEVELCACHING;i<LEVELN; i++){
		if(ismore){
			if(iter->readed_key[i]==NULL) {
				iter->target--;
				cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
				continue;
			}
			pthread_mutex_lock(&LSM.level_lock[i]);
			target_run=LSM.lop->next_run(LSM.disk[i],*iter->readed_key[i]);
		}else{
			pthread_mutex_lock(&LSM.level_lock[i]);
			target_run=LSM.lop->range_find_lowerbound(LSM.disk[i],req->key);
		}
		pthread_mutex_unlock(&LSM.level_lock[i]);

		if(target_run){
			if(iter->readed_key[i]) kvssd_free_key(iter->readed_key[i]);
			else{
				iter->readed_key[i]=(KEYT*)malloc(sizeof(KEYT));
			}
			kvssd_cpy_key(iter->readed_key[i],&target_run->end);
			param=(lsmtree_iter_param*)malloc(sizeof(lsmtree_iter_param));
			param->idx=iter->datas_idx;
			param->ppa=target_run->pbn;
			lsm_req=lsm_iter_req_factory(req,param,HEADERR);
			req->multi_value[req_param->now_level]=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
			param->value=req->multi_value[req_param->now_level];
			LSM.li->read(target_run->pbn, PAGESIZE,req->multi_value[req_param->now_level],ASYNC,lsm_req);
			req_param->now_level++;
		}
		else{
			if(iter->readed_key[i]) kvssd_free_key(iter->readed_key[i]);
			iter->readed_key[i]=NULL;
			iter->target--;
			cl_release_with_f(iter->conditional_lock, iter->target,iter->received,release_multi_handler_start);
		}
		iter->datas_idx++;
	}
}*/
/*origin data
void lsm_make_iterator(lsm_iter *iter, KEYT min){
	skiplist *skip=skiplist_init();
	for(int i=LEVELN-1; i>=0; i--){
		if(iter->datas[i]==NULL) continue;
		int start_idx=LSM.lop->find_idx_lower_bound((char*)iter->datas[i],*iter->target_key);
#ifdef KVSSD
		keyset_iter *k_iter=LSM.lop->keyset_iter_init((char*)iter->datas[i],start_idx);
		keyset *key=(keyset*)malloc(sizeof(keyset));
		for(key=LSM.lop->keyset_iter_nxt(k_iter,key); key!=NULL; key=LSM.lop->keyset_iter_nxt(k_iter, key)){
			skiplist_insert_iter(skip,key->lpa,key->ppa);
		}
		free(key);
#else
		for(int j=1; j<1024; j++){
			keyset *key=&((keyset*)(iter->datas[i]))[j];
			if(key->lpa==UINT_MAX)continue;
			if(key->lpa<min){
				printf("lpa:%u\n",key->lpa);
				continue;
			}
			skiplist_insert_iter(skip,key->lpa,key->ppa);
		}
#endif
	}

	keyset *target_keyset;
	int *now_idx,*max_idx;
	if(iter->key_array){
		iter->key_temp_array=(keyset*)malloc(sizeof(keyset)*skip->size);
		target_keyset=iter->key_temp_array;
		now_idx=&iter->t_now_idx;
		max_idx=&iter->t_max_idx;
		iter->original_first=true;
	}
	else{
		iter->key_array=(keyset*)malloc(sizeof(keyset)*skip->size);
		target_keyset=iter->key_array;
		now_idx=&iter->now_idx;
		max_idx=&iter->max_idx;
		iter->original_first=false;
	}

	printf("make iterator size : %lu\n",skip->size);
	snode *temp;
	int cnt=0;
	*now_idx=0;
	*max_idx=skip->size;

	for_each_sk(temp,skip){
		target_keyset[cnt].lpa=temp->key;
		target_keyset[cnt].ppa=temp->ppa;
		cnt++;
	}
	skiplist_free(skip);
}
uint32_t lsm_iter_read_more(request *req){
	//read more data
	Redblack t_rb;
	rb_find_int(im.rb,req->ppa,&t_rb);
	lsm_iter *iter=(lsm_iter*)t_rb->item;
	if(iter->conditional_lock){
		cl_free(iter->conditional_lock);
	}
	iter->conditional_lock=cl_init(LEVELN, true);
	iter->multi_handler_start_check=false;
	iter->target=LEVELN;
	//iter->datas_idx=0;
	iter->received=0;

	//free the mapping header
	for(int i=0; i<LEVELN; i++){
		free(iter->datas[i]);
		iter->datas[i]=NULL;
	}

	lsmtree_iter_req_param *req_param=(lsmtree_iter_req_param*)calloc(sizeof(lsmtree_iter_req_param),1);
	req_param->iter=iter;
	req->params=(void*)req_param;

	//if(!req->multi_value){
	//	req->multi_value=(value_set**)calloc(sizeof(value_set*),LEVELN);
	//}

	lsm_iter_read_header(iter,req_param,req,true);
	return 1;
}
*/
