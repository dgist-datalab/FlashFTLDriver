#include "dftl.h"
#include "../../include/utils/cond_lock.h"

#define RANGE_QUERY_LIMITER
#define RQL_SPINLOCK 1

Redblack rb_tree;
pthread_mutex rb_lock;

#ifdef RANGE_QUERY_LIMITER
#if RQL_SPINLOCK
volatile int rql_cur, rql_max;
pthread_mutex_t rql_mutex;
#else
cl_lock *rql_cond;
#endif
#endif

int rq_create() {
	rb_tree = rb_create();
	pthread_mutex_init(&rb_lock, NULL);
#ifdef RANGE_QUERY_LIMITER
#if RQL_SPINLOCK
	rql_cur = 0;
	rql_max = QSIZE;
	pthread_mutex_init(&rql_mutex, NULL);
#else
	rql_cond = cl_init(QSIZE, false);
#endif
#endif
	return 0;
}

static request *split_range_req(request *const req, KEYT key, value_set *value) {
	request *ret = (request *)malloc(sizeof(request));

	ret->type = FS_GET_T;
	ret->key = key;
	ret->value = value;
	ret->ppa = 0;

	ret->multi_value = NULL;
	ret->multi_key = NULL;
	ret->num = 1;
	ret->cpl = 0;

	ret->end_req=range_end_req;
	ret->isAsync=ASYNC;
	ret->params=NULL;
	ret->type_ftl = 0;
	ret->type_lower = 0;
	ret->before_type_lower=0;
	ret->seq=0;
	ret->special_func=NULL;
	ret->added_end_req=NULL;
	ret->p_req=NULL;
	ret->p_end_req=NULL;
#ifndef USINGAPP
	ret->mark=req->mark;
#endif

	ret->hash_params = NULL;
	ret->parents = req;

	return ret;
}

uint32_t demand_range_query(request *const req) {
	int i;
	KEYT start_key = req->key;
	int query_len = req->num;
	int valid_query_len;
	int nr_issued_query;

	int inf_write_q_hit = 0;

	request **range_req = (request **)malloc(sizeof(request *) * query_len);
	KEYT *range_key = (KEYT *)malloc(sizeof(KEYT) * query_len);

	Redblack rb_node;

	pthread_mutex_lock(&rb_lock);
	if (rb_find_str(rb_tree, start_key, &rb_node) == 0) {
		printf("rb_find_str() returns NULL\n");
		abort();
	}

	for (i = 0; i < query_len && rb_node != rb_tree; i++, rb_node = rb_next(rb_node)) {
		KEYT _key = rb_node->key;
		void *_data = qmanager_find_by_algo(_key);

		if (_data) {
			request *d_req = (request *)_data;
			memcpy(req->multi_value[i]->value, d_req->value->value, PAGESIZE);

			range_key[i].len = 0;
			range_key[i].key = NULL;

			inf_write_q_hit++;

		} else {
			range_key[i].len = _key.len;
			range_key[i].key = (char *)malloc(_key.len);
			memcpy(range_key[i].key, _key.key, _key.len);
		}
	}
	pthread_mutex_unlock(&rb_lock);

	valid_query_len = i;
	nr_issued_query = valid_query_len - inf_write_q_hit;
	req->cpl += query_len - nr_issued_query;

	if (req->cpl == req->num) {
		free(range_req);
		free(range_key);
		req->end_req(req);
		return 0;
	}

	for (i = 0; i < valid_query_len; i++) {
		if (range_key[i].len != 0) { // filter hit requests
#ifdef RANGE_QUERY_LIMITER
#if RQL_SPINLOCK
			while (rql_cur == rql_max) {
				
			}
			pthread_mutex_lock(&rql_mutex);
			rql_cur++;
			pthread_mutex_unlock(&rql_mutex);
#else
			cl_grap(rql_cond);
#endif
#endif
			range_req[i] = split_range_req(req, range_key[i], req->multi_value[i]);
			demand_get(range_req[i]);
		}
	}

	free(range_req);
	free(range_key);

	return 0;
}


bool range_end_req(request *range_req) {
	request *req = range_req->parents;

	if (range_req->type == FS_NOTFOUND_T) {
		//printf("[ERROR] Not found on Range Query! %.*s\n", range_req->key.len, range_req->key.key);
		//abort();
	}

	req->cpl++;
	if (req->num == req->cpl) {
		req->end_req(req);
	}

#ifdef RANGE_QUERY_LIMITER
#if RQL_SPINLOCK
	pthread_mutex_lock(&rql_mutex);
	rql_cur--;
	pthread_mutex_unlock(&rql_mutex);
#else
	cl_release(rql_cond);
#endif
#endif

	free(range_req->key.key);
	free(range_req);
	return NULL;
}

