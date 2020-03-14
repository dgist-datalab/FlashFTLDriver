/*
 * Demand-based FTL Range-query
 */

#include "demand.h"
#include "../../include/data_struct/redblack.h"
#include "../../include/utils/cond_lock.h"
#include "../../interface/interface.h"

#define RANGE_QUERY_LIMITER
#define RQL_SPINLOCK 1
#define RQL_MAX 128

extern algorithm __demand;

extern struct demand_env d_env;
extern struct demand_member d_member;
extern struct demand_stat d_stat;

extern cl_lock *flying;

Redblack rb_tree;
pthread_mutex_t rb_lock;
pthread_mutex_t rq_endreq_lock;
pthread_mutex_t rq_wakeup;
bool rq_is_sleep;

pthread_t range_poller_id;

#ifdef RANGE_QUERY_LIMITER
#if RQL_SPINLOCK
volatile int rql_cur, rql_max;
pthread_mutex_t rql_mutex;
#else
cl_lock *rql_cond;
#endif
#endif

void *range_poller(void *null) {
	char thread_name[128]={0};
	sprintf(thread_name,"%s","range_poller");
	pthread_setname_np(pthread_self(),thread_name);

	pthread_mutex_lock(&rq_wakeup);

	for (;;) {
		request *range_req = (request *)q_dequeue(d_member.range_q);
		if (range_req) {
			demand_read(range_req);
		}
	}
	return NULL;
}

int range_create() {
	rb_tree = rb_create();
	pthread_mutex_init(&rb_lock, NULL);
	pthread_mutex_init(&rq_endreq_lock, NULL);
	pthread_mutex_init(&rq_wakeup, NULL);
	pthread_mutex_lock(&rq_wakeup);
	rq_is_sleep = true;

	q_init(&d_member.range_q, QDEPTH * 128);

	pthread_create(&range_poller_id, NULL, &range_poller, NULL);

#ifdef RANGE_QUERY_LIMITER
#if RQL_SPINLOCK
	rql_cur = 0;
	rql_max = RQL_MAX;
	pthread_mutex_init(&rql_mutex, NULL);
#else
	rql_cond = cl_init(RQL_MAX, false);
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
	ret->num = 0;
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

	request **range_req = (request **)calloc(query_len, sizeof(request *));
	KEYT *range_key = (KEYT *)calloc(query_len, sizeof(KEYT));

	Redblack rb_node;

	if (rq_is_sleep) {
		rq_is_sleep = false;
		pthread_mutex_unlock(&rq_wakeup);
	}

	pthread_mutex_lock(&rb_lock);
	if (rb_find_str(rb_tree, start_key, &rb_node) == 0) {
		printf("[ERROR] Range-query: start_key is not found, at %s:%d\n", __FILE__, __LINE__);
		abort();
	}

	for (i = 0; i < query_len && rb_node != rb_tree; i++, rb_node = rb_next(rb_node)) {
		KEYT _key = rb_node->key;
		void *_data = qmanager_find_by_algo(_key);

		if (_data) {
			request *d_req = (request *)_data;
			memcpy(req->multi_value[i]->value, d_req->value->value, d_req->value->length);

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
	req->cpl = query_len - nr_issued_query;

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
			while (rql_cur == rql_max) { }
			pthread_mutex_lock(&rql_mutex);
			rql_cur++;
			pthread_mutex_unlock(&rql_mutex);
#else
			cl_grap(rql_cond);
#endif
#endif
			range_req[i] = split_range_req(req, range_key[i], req->multi_value[i]);
			q_enqueue((void *)range_req[i], d_member.range_q);
		}
	}

	free(range_req);
	free(range_key);

	return 0;
}

bool range_end_req(request *range_req) {
	pthread_mutex_lock(&rq_endreq_lock);
	request *req = range_req->parents;

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
	pthread_mutex_unlock(&rq_endreq_lock);

	return 0;
}
