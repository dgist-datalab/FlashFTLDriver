/*
 * Demand-based FTL Internal Implementation
 */

#include "demand.h"
#include "page.h"
#include "utility.h"
#include "../../interface/interface.h"

extern algorithm __demand;

extern demand_env env;
extern demand_member member;
extern demand_stat d_stat;


static bool cache_is_all_inflight() { return (member.nr_inflight_tpages == env.max_cached_tpages); }

static uint32_t do_wb_check(skiplist *wb, request *const req) {
	snode *wb_entry = skiplist_find(wb, req->key);
	if (WB_HIT(wb_entry)) {
		d_stat.wb_hit++;
#ifdef HASH_KVSSD
		free(req->hash_params);
#endif
		copy_value(req->value, wb_entry->value, wb_entry->value->length * GRAINED_UNIT);
		req->type_ftl = 0;
		req->type_lower = 0;
		return 1;
	}
	return 0;
}

static bool cache_is_full() {
#ifdef STRICT_CACHING
	if (member.nr_cached_tpages + member.nr_inflight_tpages > env.max_cached_tpages) abort();
#endif
	return (member.nr_cached_tpages + member.nr_inflight_tpages >= env.max_cached_tpages);
}

static uint32_t do_cache_evict(uint32_t lpa, request *const req, snode *wb_entry) {
	uint32_t rc = 0;
	blockmanager *bm = __demand.bm;

	struct cmt_struct *cmt = member.cmt[IDX(lpa)];
	struct cmt_struct *victim = (struct cmt_struct *)lru_pop(member.lru);
	struct inflight_params *i_params;

	if (victim->state == DIRTY) {
		if (!req->params) {
			req->params = malloc(sizeof(struct inflight_params));
		}
		i_params = (struct inflight_params *)req->params;
		i_params->is_evict = true;

		if (!cmt->is_flying) {
			member.nr_inflight_tpages++;
			cmt->is_flying = true;
		}

		victim->t_ppa = get_tpage(__demand.bm);
		bm->populate_bit(bm, victim->t_ppa);
		//bm->set_oob(bm, (char *)&victim->idx, sizeof(victim->idx), victim->t_ppa);
		set_oob(bm, victim->idx, victim->t_ppa, 0);

		value_set *victim_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		__demand.li->write(victim->t_ppa, PAGESIZE, victim_vs, ASYNC, make_algo_req_rw(MAPPINGW, victim_vs, req, NULL));

		rc = 1;
	}
	victim->lru_ptr = NULL;
	victim->pt = NULL;
	member.nr_cached_tpages--;

	return rc;
}

static uint32_t do_cache_load(uint32_t lpa, request *const req, snode *wb_entry) {
	struct cmt_struct *cmt = member.cmt[IDX(lpa)];
	struct inflight_params *i_params;

	if (cmt->t_ppa == UINT32_MAX) {
		return 0;
	}

	if (!req->params) {
		req->params = malloc(sizeof(struct inflight_params));
	}
	i_params = (struct inflight_params *)req->params;
	i_params->is_evict = false;
	i_params->t_ppa = cmt->t_ppa;

	if (!cmt->is_flying) {
		member.nr_inflight_tpages++;
		cmt->is_flying = true;
	}

	value_set *load_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	__demand.li->read(cmt->t_ppa, PAGESIZE, load_vs, ASYNC, make_algo_req_rw(MAPPINGR, load_vs, req, NULL));
	return 1;
}

static uint32_t do_cache_register(uint32_t lpa) {
	struct cmt_struct *cmt = member.cmt[IDX(lpa)];
	cmt->pt = member.mem_table[IDX(lpa)];
	cmt->lru_ptr = lru_push(member.lru, (void *)cmt);
	member.nr_cached_tpages++;

	if (cmt->is_flying) {
		cmt->is_flying = false;
		member.nr_inflight_tpages--;

		request *retry_req;
		while ((retry_req = (request *)q_dequeue(cmt->blocked_q))) {
			inf_assign_try(retry_req);
		}
	}
	return 0;
}

static uint32_t read_actual_dpage(uint32_t ppa, request *const req) {
	if (ppa != UINT32_MAX) {
		struct algo_req *a_req = make_algo_req_rw(DATAR, NULL, req, NULL);
#ifdef DVALUE
		((struct demand_params *)a_req->params)->offset = ppa % GRAIN_PER_PAGE;
		ppa = ppa / GRAIN_PER_PAGE;
#endif
		__demand.li->read(ppa, PAGESIZE, req->value, ASYNC, a_req);
	}
	return ppa;
}

uint32_t __demand_read(request *const req) {
	uint32_t rc = 0;

	lpa_t lpa;
	ppa_t ppa;
	struct cmt_struct *cmt;

	struct hash_params *h_params = (struct hash_params *)req->hash_params;

#ifdef STORE_KEY_FP
read_retry:
#endif
	lpa = get_lpa(req->key, req->hash_params);
	ppa = UINT32_MAX;

	cmt = member.cmt[IDX(lpa)];

	if (h_params->cnt > member.max_try) {
		rc = UINT32_MAX;
		goto read_ret;
	}

	/* inflight request */
	if (IS_INFLIGHT(req->params)) {
		struct inflight_params *i_params = (struct inflight_params *)req->params;
		if (i_params->is_evict) {
			goto cache_load;
		}
		if (i_params->t_ppa != cmt->t_ppa) {
			i_params->t_ppa = cmt->t_ppa;
			goto cache_load;
		}
		free(req->params);
		req->params = NULL;
		goto cache_register;
	}

	if (cache_is_all_inflight()) {
		q_enqueue((void *)req, member.blocked_q);
		goto read_ret;
	}

	/* 1. check write buffer first */
	rc = do_wb_check(member.write_buffer, req);
	if (rc) {
		req->end_req(req);
		goto read_ret;
	}

	/* 2. check cache */
	if (cmt->is_flying) {
		d_stat.blocked_miss++;
		q_enqueue((void *)req, cmt->blocked_q);
		goto read_ret;
	}

	if (CACHE_HIT(cmt->pt)) {
		d_stat.cache_hit++;

		ppa = cmt->pt[OFFSET(lpa)].ppa;
		lru_update(member.lru, cmt->lru_ptr);

	} else {
		d_stat.cache_miss++;
		if (cmt->t_ppa == UINT32_MAX) {
			rc = UINT32_MAX;
			goto read_ret;
		}
		if (cache_is_full()) {
			rc = do_cache_evict(lpa, req, NULL);
			if (rc) {
				goto read_ret;
			}
		}
cache_load:
		rc = do_cache_load(lpa, req, NULL);
		if (rc) {
			goto read_ret;
		}
cache_register:
		rc = do_cache_register(lpa);
		ppa = cmt->pt[OFFSET(lpa)].ppa;
	}

#ifdef STORE_KEY_FP
	if (h_params->key_fp != cmt->pt[OFFSET(lpa)].key_fp) {
		h_params->cnt++;
		goto read_retry;
	}
#endif

	/* 3. read actual data */
	rc = read_actual_dpage(ppa, req);

read_ret:
	return rc;
}

static bool wb_is_full(skiplist *wb) { return (wb->size == env.wb_flush_size); }

#ifdef DVALUE
struct flush_node {
	//lpa_t *lpa_list;
	ppa_t ppa;
	value_set *value;
};
struct flush_list {
	int size;
	struct flush_node *list;
} fl;
#endif

static void _do_wb_assign_ppa(skiplist *wb) {
	blockmanager *bm = __demand.bm;

	snode *wb_entry;
	sk_iter *iter = skiplist_get_iterator(wb);

#ifdef DVALUE
	l_bucket *wb_bucket = (l_bucket *)malloc(sizeof(l_bucket));
	for (int i = 0; i <= GRAIN_PER_PAGE; i++) {
		wb_bucket->idx[i] = 0;
	}

	for (size_t i = 0; i < env.wb_flush_size; i++) {
		wb_entry = skiplist_get_next(iter);
		int val_len = wb_entry->value->length;

		wb_bucket->bucket[val_len][wb_bucket->idx[val_len]] = wb_entry;
		wb_bucket->idx[val_len]++;

		//if (unlikely(val_len!=16)) abort();
	}

	fl.size = 0;
	fl.list = (struct flush_node *)calloc(env.wb_flush_size, sizeof(struct flush_node));
/*	for (int i = 0; i < env.wb_flush_size; i++) {
		fl.list[i].lpa_list = (lpa_t *)malloc(64);
		for (int j = 0; j < GRAIN_PER_PAGE; j++) {
			fl.list[i].lpa_list[j] = UINT32_MAX;
		}
	} */

	int ordering_done = 0;
	while (ordering_done < env.wb_flush_size) {
		value_set *new_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		PTR page = new_vs->value;
		int remain = PAGESIZE;
		ppa_t ppa = get_dpage(bm);
		int offset = 0;

		fl.list[fl.size].ppa = ppa;
		fl.list[fl.size].value = new_vs;

		while (remain > 0) {
			int target_length = remain / GRAINED_UNIT;
			while(wb_bucket->idx[target_length]==0 && target_length!=0) --target_length;
			if (target_length==0) {
				break;
			}

			wb_entry = wb_bucket->bucket[target_length][wb_bucket->idx[target_length]-1];
			wb_bucket->idx[target_length]--;
			wb_entry->ppa = ppa * GRAIN_PER_PAGE + offset;

			memcpy(&page[offset*GRAINED_UNIT], wb_entry->value->value, wb_entry->value->length * GRAINED_UNIT);

			lpa_t lpa = get_lpa(wb_entry->key, wb_entry->hash_params);
			struct cmt_struct *cmt = member.cmt[IDX(lpa)];
			q_enqueue((void *)wb_entry, cmt->wait_q);

			//fl.list[fl.size].lpa_list[offset] = lpa;
			//set_oob(bm, lpa, ppa, offset);
			validate_grain(bm, ppa * GRAIN_PER_PAGE + offset);

			((struct hash_params *)wb_entry->hash_params)->fl_idx = fl.size;

			offset += target_length;
			remain -= target_length * GRAINED_UNIT;

			ordering_done++;
		}

		fl.size++;
	}
	free(wb_bucket);
#else
	for (int i = 0; i < env.wb_flush_size; i++) {
		wb_entry = skiplist_get_next(iter);
		wb_entry->ppa = get_dpage(bm);

		lpa_t lpa = get_lpa(wb_entry->key, wb_entry->hash_params);
		struct cmt_struct *cmt = member.cmt[IDX(lpa)];
		q_enqueue((void *)wb_entry, cmt->wait_q);
	}
#endif
	free(iter);
}

static uint32_t _do_wb_mapping_update(skiplist *wb) {
	blockmanager *bm = __demand.bm;

	snode *wb_entry;
	struct cmt_struct *cmt;

	volatile int updated = 0;
	volatile int wb_retry_cnt, wb_register;
	volatile int remain = env.wb_flush_size;

wb_retry:
	/* bulk load a set of tpages */
	for (int i = 0; i < env.nr_valid_tpages; i++) {
		cmt = member.cmt[i];
		if (cmt->wait_q->size > 0) {
			if (IS_INITIAL_PPA(cmt->t_ppa) || CACHE_HIT(cmt->pt)) {
				if (unlikely(cmt->state == DIRTY)) abort();
				q_enqueue((void *)cmt, member.wb_cmt_load_q);
				continue;
			}
			value_set *_value_mr = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			__demand.li->read(cmt->t_ppa, PAGESIZE, _value_mr, ASYNC, make_algo_req_cmt(MAPPINGR, _value_mr, cmt));
		}
	}

	/* bulk update */
	updated = 0; wb_retry_cnt = 0;
	while (updated + wb_retry_cnt < remain) {
		cmt = (struct cmt_struct *)q_dequeue(member.wb_cmt_load_q);
		if (cmt == NULL) continue;

		struct pt_struct *pt = member.mem_table[cmt->idx];
		while ((wb_entry = (snode *)q_dequeue(cmt->wait_q))) {
			lpa_t lpa = get_lpa(wb_entry->key, wb_entry->hash_params);
			ppa_t origin = pt[OFFSET(lpa)].ppa;
#ifdef HASH_KVSSD
			struct hash_params *h_params = (struct hash_params *)wb_entry->hash_params;
	#ifdef STORE_KEY_FP
			/* can skip the works reading the target page to verify if the key is correct or not */
			fp_t key_fp = pt[OFFSET(lpa)].key_fp;
			if (!IS_INITIAL_PPA(origin) && key_fp != h_params->key_fp) {
				h_params->find = HASH_KEY_DIFF;
				h_params->cnt++;
				q_enqueue((void *)wb_entry, member.wb_retry_q);
				wb_retry_cnt++;
				continue;
			}
	#endif
			/* read the target page to verify it */
			if (!IS_INITIAL_PPA(origin)) {
				value_set *_value_dr_check = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	#ifdef DVALUE
				algo_req *a_req = make_algo_req_rw(DATAR, _value_dr_check, NULL, wb_entry);
				((struct demand_params *)a_req->params)->offset = origin % GRAIN_PER_PAGE;
				__demand.li->read(origin/GRAIN_PER_PAGE, PAGESIZE, _value_dr_check, ASYNC, a_req);
	#else
				__demand.li->read(origin, PAGESIZE, _value_dr_check, ASYNC, make_algo_req_rw(DATAR, _value_dr_check, NULL, wb_entry));
	#endif
				wb_retry_cnt++;
				continue;
			}
#else
			if (IS_INITIAL_PPA(origin) && bm->is_valid_page(bm, origin)) {
				bm->unpopulate_bit(bm, origin);
			}
#endif

			pt[OFFSET(lpa)].ppa = wb_entry->ppa;
#ifdef STORE_KEY_FP
			pt[OFFSET(lpa)].key_fp = h_params->key_fp;
#endif
			if (!IS_INITIAL_PPA(cmt->t_ppa)) {
				int rc = bm->unpopulate_bit(bm, cmt->t_ppa);
				if (!rc) abort();
				cmt->t_ppa = UINT32_MAX;
			}
			cmt->state = DIRTY;
			updated++;

			if (h_params->cnt < MAX_HASH_COLLISION) d_stat.w_hash_collision_cnt[h_params->cnt]++;
			//else abort();
			member.max_try = (h_params->cnt > member.max_try) ? h_params->cnt : member.max_try;

#ifdef DVALUE
			/* oob setting */
			//((lpa_t *)fl.list[h_params->fl_idx].lpa_list)[origin%GRAIN_PER_PAGE] = lpa;
			set_oob(bm, lpa, wb_entry->ppa/GRAIN_PER_PAGE, wb_entry->ppa%GRAIN_PER_PAGE); 
#endif
		}
	}
	remain -= updated;

#ifdef HASH_KVSSD
	updated = 0; wb_register = 0;
	while (updated + wb_register < wb_retry_cnt) {
		wb_entry = (snode *)q_dequeue(member.wb_retry_q);
		if (wb_entry == NULL) continue;

		lpa_t lpa = get_lpa(wb_entry->key, wb_entry->hash_params);
		cmt = member.cmt[IDX(lpa)];
		struct pt_struct *pt = member.mem_table[cmt->idx];
		struct hash_params *h_params = (struct hash_params *)wb_entry->hash_params;

		if (h_params->find == HASH_KEY_SAME) {
			ppa_t origin = pt[OFFSET(lpa)].ppa; // Never be INITIAL
#ifdef DVALUE
			invalidate_grain(bm, origin);
#else
			bm->unpopulate_bit(bm, origin);
#endif
			pt[OFFSET(lpa)].ppa = wb_entry->ppa;
			//pt[OFFSET(lpa)].key_fp = h_params->key_fp;

			if (!IS_INITIAL_PPA(cmt->t_ppa)) {
				int rc = bm->unpopulate_bit(bm, cmt->t_ppa);
				if (!rc) abort();
				cmt->t_ppa = UINT32_MAX;
			}
			cmt->state = DIRTY;
			updated++;

			if (h_params->cnt < MAX_HASH_COLLISION) d_stat.w_hash_collision_cnt[h_params->cnt]++;
			//else abort();
			member.max_try = (h_params->cnt > member.max_try) ? h_params->cnt : member.max_try;

#ifdef DVALUE
			/* oob setting */
			//((lpa_t *)fl.list[h_params->fl_idx].lpa_list)[origin%GRAIN_PER_PAGE] = lpa;
			set_oob(bm, lpa, wb_entry->ppa/GRAIN_PER_PAGE, wb_entry->ppa%GRAIN_PER_PAGE);
#endif
		
		} else if (h_params->find == HASH_KEY_DIFF) {
			q_enqueue((void *)wb_entry, cmt->wait_q);
			wb_register++;
		}
	}
	remain -= updated;
#endif

	/* bulk write temporarily loaded tpages */
	for (int i = 0; i < env.nr_valid_tpages; i++) {
		cmt = member.cmt[i];
		if (cmt->state == DIRTY && !CACHE_HIT(cmt->pt)) {
			cmt->t_ppa = get_tpage(bm);
			cmt->state = CLEAN;

			value_set *_value_mw = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
			__demand.li->write(cmt->t_ppa, PAGESIZE, _value_mw, ASYNC, make_algo_req_rw(MAPPINGW, _value_mw, NULL, NULL));

			bm->populate_bit(bm, cmt->t_ppa);
			//bm->set_oob(bm, (char *)&cmt->idx, sizeof(cmt->idx), cmt->t_ppa);
			set_oob(bm, cmt->idx, cmt->t_ppa, 0);
		}
	}

#ifdef HASH_KVSSD
	if (wb_register > 0) goto wb_retry;
#ifdef DVALUE
	sk_iter *iter = skiplist_get_iterator(wb);
	for (int i = 0; i < env.wb_flush_size; i++) {
		wb_entry = skiplist_get_next(iter);
		free(wb_entry->hash_params);
	}
#endif
#endif

	return 0;
}

static skiplist *_do_wb_flush(skiplist *wb) {
	blockmanager *bm = __demand.bm;
#ifdef DVALUE
	for (int i = 0; i < fl.size; i++) {
		//lpa_t *lpa_list = fl.list[i].lpa_list;
		ppa_t ppa = fl.list[i].ppa;
		value_set *value = fl.list[i].value;

		__demand.li->write(ppa, PAGESIZE, value, ASYNC, make_algo_req_rw(DATAW, value, NULL, NULL));
		//bm->set_oob(bm, (char *)lpa_list, 64, ppa);
		//bm->populate_bit(bm, ppa);
/*		for (int j = 0; j < GRAIN_PER_PAGE; j++) {
			if (lpa_list[j] != UINT32_MAX) {
				validate_grain(bm, ppa * GRAIN_PER_PAGE + j);
			}
		} */
		//free(lpa_list);
	}
	free(fl.list);
#else
	snode *wb_entry;
	sk_iter *iter = skiplist_get_iterator(wb);
	for (size_t i = 0; i < env.wb_flush_size; i++) {
		wb_entry = skiplist_get_next(iter);

		lpa_t lpa = get_lpa(wb_entry->key, wb_entry->hash_params);
		ppa_t ppa = wb_entry->ppa;
		value_set *value = wb_entry->value;

		__demand.li->write(ppa, PAGESIZE, value, ASYNC, make_algo_req_rw(DATAW, value, NULL, wb_entry));
		//bm->set_oob(bm, (char *)&lpa, sizeof(lpa), ppa);
		set_oob(bm, lpa, ppa, 0);
		bm->populate_bit(bm, ppa);

		wb_entry->value = NULL;
	}
	free(iter);
#endif

	/* wait until device traffic clean */
	__demand.li->lower_flying_req_wait();

	skiplist_free(wb);
	wb = skiplist_init();

	return wb;
}

static uint32_t _do_wb_insert(skiplist *wb, request *const req) {
	snode *wb_entry = skiplist_insert(wb, req->key, req->value, true);
#ifdef HASH_KVSSD
	wb_entry->hash_params = (void *)req->hash_params;
#endif
	req->value = NULL;

	if (wb_is_full(wb)) return 1;
	else return 0;
}

uint32_t __demand_write(request *const req) {
	uint32_t rc = 0;
	skiplist *wb = member.write_buffer;

	/* flush the buffer if full */
	if (wb_is_full(wb)) {
		/* assign ppa first */
		_do_wb_assign_ppa(wb);

		/* mapping update [lpa, origin]->[lpa, new] */
		_do_wb_mapping_update(wb);
		
		/* flush the buffer */
		wb = member.write_buffer = _do_wb_flush(wb);
	}

	/* default: insert to the buffer */
	rc = _do_wb_insert(wb, req); // rc: is the write buffer is full? 1 : 0

	req->end_req(req);
	return rc;
}

uint32_t __demand_remove(request *const req) {
	puts("Hello! remove() is not implemented yet! lol!");
	return 0;
}

void *demand_end_req(algo_req *a_req) {
	struct demand_params *d_params = (struct demand_params *)a_req->params;
	request *req = a_req->parents;
	snode *wb_entry = d_params->wb_entry;
	struct cmt_struct *cmt = d_params->cmt;

	struct hash_params *h_params;
	KEYT check_key;

#ifdef DVALUE
	int offset = d_params->offset;
#endif

	switch (a_req->type) {
	case DATAR:
		d_stat.data_r++;
#ifdef HASH_KVSSD
		if (IS_READ(req)) {
			req->type_ftl++;

			h_params = (struct hash_params *)req->hash_params;
#ifdef DVALUE
			copy_key_from_grain(&check_key, req->value, offset);
#else
			copy_key_from_value(&check_key, req->value);
#endif

			if (KEYCMP(req->key, check_key) == 0) {
				if (h_params->cnt < MAX_HASH_COLLISION) {
					d_stat.r_hash_collision_cnt[h_params->cnt]++;
				}
				free(h_params);
				req->end_req(req);
			} else {
				h_params->find = HASH_KEY_DIFF;
				h_params->cnt++;
				inf_assign_try(req);
			}
		} else {
			h_params = (struct hash_params *)wb_entry->hash_params;
#ifdef DVALUE
			copy_key_from_grain(&check_key, d_params->value, offset);
#else
			copy_key_from_value(&check_key, d_params->value);
#endif

			if (KEYCMP(wb_entry->key, check_key) == 0) {
				h_params->find = HASH_KEY_SAME;
			} else {
				h_params->find = HASH_KEY_DIFF;
				h_params->cnt++;
			}

			inf_free_valueset(d_params->value, FS_MALLOC_R);

			q_enqueue((void *)wb_entry, member.wb_retry_q);
		}
		free(check_key.key);
#else
		req->end_req(req);
#endif
		break;
	case DATAW:
		d_stat.data_w++;
		inf_free_valueset(d_params->value, FS_MALLOC_W);
#ifndef DVALUE
		free(wb_entry->hash_params);
#endif
		break;
	case MAPPINGR:
		d_stat.trans_r++;
		inf_free_valueset(d_params->value, FS_MALLOC_R);
		if (IS_READ(req)) {
			inf_assign_try(req);
			req->type_ftl++;
		}
		else if (cmt) q_enqueue((void *)cmt, member.wb_cmt_load_q);
		break;
	case MAPPINGW:
		d_stat.trans_w++;
		inf_free_valueset(d_params->value, FS_MALLOC_W);
		if (IS_READ(req)) {
			inf_assign_try(req);
			req->type_ftl+=100;
		}
		break;
	case GCDR:
		d_stat.data_r_dgc++;
		member.nr_valid_read_done++;
		break;
	case GCDW:
		d_stat.data_w_dgc++;
		inf_free_valueset(d_params->value, FS_MALLOC_W);
		break;
	case GCMR_DGC:
		d_stat.trans_r_dgc++;
		member.nr_tpages_read_done++;
		inf_free_valueset(d_params->value, FS_MALLOC_R);
		break;
	case GCMW_DGC:
		d_stat.trans_w_dgc++;
		inf_free_valueset(d_params->value, FS_MALLOC_W);
		break;
	case GCMR:
		d_stat.trans_r_tgc++;
		member.nr_valid_read_done++;
		break;
	case GCMW:
		d_stat.trans_w_tgc++;
		inf_free_valueset(d_params->value, FS_MALLOC_W);
		break;
	default:
		abort();
	}

	free_algo_req(a_req);
	return NULL;
}
