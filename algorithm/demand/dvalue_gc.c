/*
 * Dynamic Value GC Implementation
 */

#ifdef DVALUE

#include "demand.h"
#include "page.h"
#include "utility.h"
#include "cache.h"
#include "../../interface/interface.h"

extern algorithm __demand;

extern struct demand_env d_env;
extern struct demand_member d_member;
extern struct demand_stat d_stat;

extern struct demand_cache *d_cache;

extern __segment *d_active;
extern __segment *d_reserve;

struct gc_bucket_node {
	PTR ptr;
	int32_t lpa;
	int32_t ppa;
};

struct gc_bucket {
	struct gc_bucket_node bucket[PAGESIZE/GRAINED_UNIT+1][_PPS*GRAIN_PER_PAGE];
	uint32_t idx[PAGESIZE/GRAINED_UNIT+1];
};

extern struct algo_req *make_algo_req_default(uint8_t, value_set *);
static int 
_do_bulk_read_pages_containing_valid_grain
(blockmanager *bm, struct gc_table_struct **bulk_table, __gsegment *target_seg) {

	__block *target_blk = NULL;
	int ppa = 0;
	int blk_idx = 0;
	int page_idx = 0;

	int i = 0;
	for_each_page_in_seg_blocks(target_seg, target_blk, ppa, blk_idx, page_idx) {
		if (contains_valid_grain(bm, ppa)) {
			value_set *origin = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			__demand.li->read(ppa, PAGESIZE, origin, ASYNC, make_algo_req_default(GCDR, origin));

			bulk_table[i] = (struct gc_table_struct *)malloc(sizeof(struct gc_table_struct));
			bulk_table[i]->origin   = origin;
			//bulk_table[i]->lpa_list = (lpa_t *)bm->get_oob(bm, ppa);
			bulk_table[i]->ppa      = ppa;

			i++;
		}
	}
	int nr_read_pages = i;
	return nr_read_pages;
}

static void _do_wait_until_read_all(int target_number) {
	while (d_member.nr_valid_read_done != target_number) {}
	d_member.nr_valid_read_done = 0;
}

struct gc_bucket *gc_bucket;
struct gc_bucket_node *gcb_node_arr[_PPS*GRAIN_PER_PAGE];
static int 
_do_bulk_write_valid_grains
(blockmanager *bm, struct gc_table_struct **bulk_table, int nr_read_pages, page_t type) {
	gc_bucket = (struct gc_bucket *)malloc(sizeof(struct gc_bucket));
	for (int i = 0; i < PAGESIZE/GRAINED_UNIT+1; i++) gc_bucket->idx[i] = 0;

	int nr_valid_grains = 0;
	for (int i = 0; i < nr_read_pages; i++) {
		for (int j = 0; j < GRAIN_PER_PAGE; j++) {
			pga_t pga = bulk_table[i]->ppa * GRAIN_PER_PAGE + j;
			if (is_valid_grain(pga)) {
				int len = 1;
				lpa_t *lpa_list = get_oob(bm, pga/GRAIN_PER_PAGE);

				while (j+len < GRAIN_PER_PAGE && lpa_list[j+len] == 0) { len++; }

				struct gc_bucket_node *gcb_node = &gc_bucket->bucket[len][gc_bucket->idx[len]];
				gcb_node->ptr = bulk_table[i]->origin->value + j*GRAINED_UNIT;
				gcb_node->lpa = lpa_list[j];
				if (unlikely(lpa_list[j] == 0)) abort();

				gc_bucket->idx[len]++;

				gcb_node_arr[nr_valid_grains++] = gcb_node;

				invalidate_page(bm, pga, DATA);
			}
		}
		if (unlikely(contains_valid_grain(bm, bulk_table[i]->ppa))) abort();
	}

	int ordering_done = 0, copied_pages = 0;
	while (ordering_done < nr_valid_grains) {
		value_set *_value_dgcw = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		PTR page = _value_dgcw->value;
		int remain = PAGESIZE;
		ppa_t ppa = bm->get_page_num(bm, d_reserve);
		int offset = 0;

		while (remain > 0) {
			int target_length = remain / GRAINED_UNIT;
			while (gc_bucket->idx[target_length] == 0 && target_length != 0) { target_length--; }
			if (target_length == 0) break;

			struct gc_bucket_node *gcb_node = &gc_bucket->bucket[target_length][gc_bucket->idx[target_length]-1];
			gc_bucket->idx[target_length]--;
			gcb_node->ppa = PPA_TO_PGA(ppa, offset);

			/** VALID COPY HERE **/
			memcpy(&page[offset*GRAINED_UNIT], gcb_node->ptr, target_length*GRAINED_UNIT);

			//set_oob(bm, gcb_node->lpa, ppa, type);
			set_oob(bm, gcb_node->lpa, gcb_node->ppa, type);
			validate_grain(bm, gcb_node->ppa);

			offset += target_length;
			remain -= target_length * GRAINED_UNIT;

			ordering_done++;
		}
		__demand.li->write(ppa, PAGESIZE, _value_dgcw, ASYNC, make_algo_req_default(GCDW, _value_dgcw));

		copied_pages++;
	}
	printf("[valid grains: %d -> packed pages: %d]\n", nr_valid_grains, copied_pages);

	return nr_valid_grains;
}

static int lpa_compare(const void *a, const void *b) {
	lpa_t a_lpa = (*(struct gc_bucket_node **)a)->lpa;
	lpa_t b_lpa = (*(struct gc_bucket_node **)b)->lpa;

	if (a_lpa < b_lpa) return -1;
	else if (a_lpa == b_lpa) return 0;
	else return 1;
}

static int _do_bulk_mapping_update(blockmanager *bm, int nr_valid_grains, page_t type) {
	qsort(gcb_node_arr, nr_valid_grains, sizeof(struct gc_bucket_node *), lpa_compare);

	bool *skip_update = (bool *)calloc(nr_valid_grains, sizeof(bool));

	/* read mapping table which needs update */
	volatile int nr_update_tpages = 0;
	for (int i = 0; i < nr_valid_grains; i++) {
		struct gc_bucket_node *gcb_node = gcb_node_arr[i];
		lpa_t lpa = gcb_node->lpa;

		if (d_cache->is_hit(lpa)) {
			struct pt_struct pte = d_cache->get_pte(lpa);
			pte.ppa = gcb_node->ppa;
			d_cache->update(lpa, pte);

			skip_update[i] = true;

		} else {
			struct cmt_struct *cmt = d_cache->get_cmt(lpa);
			if (cmt->t_ppa == UINT32_MAX) {
				continue;
			}
			value_set *_value_mr = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			__demand.li->read(cmt->t_ppa, PAGESIZE, _value_mr, ASYNC, make_algo_req_default(GCMR_DGC, _value_mr));

			invalidate_page(bm, cmt->t_ppa, MAP);
			cmt->t_ppa = UINT32_MAX;
			
			nr_update_tpages++;
		}
	}

	/* wait */
	while (d_member.nr_tpages_read_done != nr_update_tpages) {}
	d_member.nr_tpages_read_done = 0;

	/* write */
	for (int i = 0; i < nr_valid_grains; i++) {
		if (skip_update[i]) {
			continue;
		}

		struct cmt_struct *cmt = d_cache->member.cmt[IDX(gcb_node_arr[i]->lpa)];
		struct pt_struct *pt = d_cache->member.mem_table[cmt->idx];

		pt[OFFSET(gcb_node_arr[i]->lpa)].ppa = gcb_node_arr[i]->ppa;
		while (i+1 < nr_valid_grains && IDX(gcb_node_arr[i+1]->lpa) == cmt->idx) {
			pt[OFFSET(gcb_node_arr[i+1]->lpa)].ppa = gcb_node_arr[i+1]->ppa;
			i++;
		}

		cmt->t_ppa = get_tpage(bm);
		value_set *_value_mw = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		__demand.li->write(cmt->t_ppa, PAGESIZE, _value_mw, ASYNC, make_algo_req_default(GCMW_DGC, _value_mw));

		set_oob(bm, cmt->idx, cmt->t_ppa, MAP);

		cmt->state = CLEAN;
	}

	free(skip_update);
	return 0;
}

int dpage_gc_dvalue(blockmanager *bm) {
	d_stat.dgc_cnt++;

	struct gc_table_struct **bulk_table = (struct gc_table_struct **)calloc(_PPS, sizeof(gc_table_struct *));

	__gsegment *target_seg = bm->pt_get_gc_target(bm, DATA_S);

	int nr_read_pages = _do_bulk_read_pages_containing_valid_grain(bm, bulk_table, target_seg);

	_do_wait_until_read_all(nr_read_pages);

	int nr_valid_grains = _do_bulk_write_valid_grains(bm, bulk_table, nr_read_pages, DATA);

	_do_bulk_mapping_update(bm, nr_valid_grains, DATA);

	/* trim blocks on the gsegemnt */
	bm->pt_trim_segment(bm, DATA_S, target_seg, __demand.li);

	/* reserve -> active / target_seg -> reserve */
	d_active  = d_reserve;
	d_reserve = bm->change_pt_reserve(bm, DATA_S, d_reserve);

	for (int i = 0; i < nr_read_pages; i++) {
		inf_free_valueset(bulk_table[i]->origin, FS_MALLOC_R);
		free(bulk_table[i]);
	}
	free(bulk_table);

	free(gc_bucket);

	return nr_read_pages;
}

#endif
