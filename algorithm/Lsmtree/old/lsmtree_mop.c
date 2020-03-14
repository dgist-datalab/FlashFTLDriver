#include "lsmtree.h"
#include "compaction.h"
//#include ""
#include "../../include/container.h"
#include "../../include/settings.h"
#include "../../bench/bench.h"

extern lsmtree LSM;
typedef struct mget_params{
	int done_num;
	pthread_mutex_t lock;
}mget_params;

uint32_t lsm_multi_set(request *const req, int num){
	bench_algo_start(req);
	for(int i=0;i<num; i++){
		compaction_check(req->multi_key[i],false);
		skiplist_insert(LSM.memtable,req->multi_key[i],req->multi_value[i],true);
	}
	MP(&req->latency_ftl);
	req->end_req(req);
	bench_algo_end(req);
	return 1;
}

void *lsm_mget_end_req(algo_req *const req){
	lsm_params *params=(lsm_params*)req->params;
	request *new_req=req->parents;
	request *org_req=(request*)new_req->p_req;

	mget_params *mparams=(mget_params*)org_req->params;
	if(req->params==NULL){
		//when the value is in-memory;
		mparams->done_num++;
	}else{
		switch(params->lsm_type){
			case DATAR:
				mparams->done_num++;
				break;
			default:
				printf("[%s]:%d\n",__FILE__,__LINE__);
				abort();
				break;
		}
	}
	free(new_req);
	free(params);

	pthread_mutex_lock(&mparams->lock);
	if(mparams->done_num==org_req->num){
		pthread_mutex_unlock(&mparams->lock);
		org_req->end_req(org_req);
	}else{
		pthread_mutex_unlock(&mparams->lock);
	}
	return NULL;
}

request *make_req_by_mget(request *const req, int idx){
	request *res=(request*)calloc(sizeof(request),1);
	res->type=req->type;
	res->key=req->multi_key[idx];
	res->value=req->multi_value[idx];
	res->p_req=req;
	return res;
}

uint32_t lsm_multi_get(request *const req, int num){
	mget_params *mparams=(mget_params*)malloc(sizeof(mget_params));
	mparams->done_num=0;
	pthread_mutex_init(&mparams->lock,NULL);
	req->params=(void*)mparams;
	for(int i=0;i<num; i++){
		request *new_req=make_req_by_mget(req,i);
		lsm_get(new_req);
	}
	return 1;
}
