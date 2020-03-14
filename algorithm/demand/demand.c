/*
 * Demand-based FTL Interface
 */

#include "demand.h"
#include "page.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "../../interface/interface.h"
#ifdef HASH_KVSSD
#include "../../include/utils/sha256.h"
#endif

struct algorithm __demand = {
	.argument_set = demand_argument_set,
	.create = demand_create,
	.destroy = demand_destroy,
	.read = demand_read,
	.write = demand_write,
	.remove  = demand_remove,
	.iter_create = NULL,
	.iter_next = NULL,
	.iter_next_with_value = NULL,
	.iter_release = NULL,
	.iter_all_key = NULL,
	.iter_all_value = NULL,
	.multi_set = NULL,
	.multi_get = NULL,
	.range_query = demand_range_query
};

struct demand_env d_env;
struct demand_member d_member;
struct demand_stat d_stat;

struct demand_cache *d_cache;

#ifdef HASH_KVSSD
KEYT key_max, key_min;
#endif


uint32_t demand_argument_set(int argc, char **argv) {
	int c;
	bool ci_flag = false;
	bool cr_flag = false;

	while ((c=getopt(argc, argv, "cr")) != -1) {
		switch (c) {
		case 'c':
			ci_flag = true;
			printf("cache id:%s\n", argv[optind]);
			d_env.cache_id = atoi(argv[optind]);
			break;
		case 'r':
			cr_flag = true;
			printf("caching ratio:%s\n", argv[optind]);
			d_env.caching_ratio = (float)atoi(argv[optind])/100;
			break;
		}
	}

	if (!ci_flag) d_env.cache_id = COARSE_GRAINED;
	if (!cr_flag) d_env.caching_ratio = 0.25;

	return 0;
}


static void print_demand_env(const struct demand_env *_env) {
	puts("");

#ifdef HASH_KVSSD
	printf(" |---------- algorithm_log : Hash-based Demand KV FTL\n");
#else
	printf(" |---------- algorithm_log : Demand-based FTL\n");
#endif
	printf(" | Total Segments:         %d\n", _env->nr_segments);
	printf(" |  -Translation Segments: %d\n", _env->nr_tsegments);
	printf(" |  -Data Segments:        %d\n", _env->nr_dsegments);
	printf(" | Total Pages:            %d\n", _env->nr_pages);
	printf(" |  -Translation Pages:    %d\n", _env->nr_tpages);
	printf(" |  -Data Pages:           %d\n", _env->nr_dpages);
#ifdef DVALUE
	printf(" |    -Data Grains:        %d\n", _env->nr_dgrains);
#endif
	printf(" |  -Page per Segment:     %d\n", _PPS);
/*	printf(" | Total cache pages:      %d\n", _env->nr_valid_tpages);
	printf(" |  -Mixed Cache pages:    %d\n", _env->max_cached_tpages);
	printf(" |  -Cache Percentage:     %0.3f%%\n", _env->caching_ratio * 100); */
	printf(" | WriteBuffer flush size: %ld\n", _env->wb_flush_size);
	printf(" |\n");
	printf(" | ! Assume no Shadow buffer\n");
	printf(" |---------- algorithm_log END\n");

	puts("");
}

static void demand_env_init(struct demand_env *const _env) {
	_env->nr_pages = _NOP;
	_env->nr_blocks = _NOB;
	_env->nr_segments = _NOS;

	_env->nr_tsegments = MAPPART_SEGS;
	_env->nr_tpages = _env->nr_tsegments * _PPS;
	_env->nr_dsegments = _env->nr_segments - _env->nr_tsegments;
	_env->nr_dpages = _env->nr_dsegments * _PPS;

/*	_env->caching_ratio = CACHING_RATIO;
	_env->nr_tpages_optimal_caching = _env->nr_pages * 4 / PAGESIZE;
	_env->nr_valid_tpages = _env->nr_pages * ENTRY_SIZE / PAGESIZE;
	_env->max_cached_tpages = _env->nr_tpages_optimal_caching * _env->caching_ratio; */

#ifdef WRITE_BACK
	_env->wb_flush_size = MAX_WRITE_BUF;
#else
	_env->wb_flush_size = 1;
#endif

#ifdef PART_CACHE
	_env->part_ratio = PART_RATIO;
	_env->max_clean_tpages = _env->max_cached_tpages * _env->part_ratio;
	_env->max_dirty_tentries = (_env->max_cached_tpages - _env->max_clean_tpages) * PAGESIZE / (ENTRY_SIZE + 4); // (Dirty cache size) / (Entry size)
#endif

#ifdef DVALUE
	_env->nr_grains = _env->nr_pages * GRAIN_PER_PAGE;
	_env->nr_dgrains = _env->nr_dpages * GRAIN_PER_PAGE;
	//_env->nr_valid_tpages *= GRAIN_PER_PAGE;
#endif

	print_demand_env(_env);
}


static int demand_member_init(struct demand_member *const _member) {

#ifdef HASH_KVSSD
	key_max.key = (char *)malloc(sizeof(char) * MAXKEYSIZE);
	key_max.len = MAXKEYSIZE;
	memset(key_max.key, -1, sizeof(char) * MAXKEYSIZE);

	key_min.key = (char *)malloc(sizeof(char) * MAXKEYSIZE);
	key_min.len = MAXKEYSIZE;
	memset(key_min.key, 0, sizeof(char) * MAXKEYSIZE);
#endif

	pthread_mutex_init(&_member->op_lock, NULL);

	_member->write_buffer = skiplist_init();

	q_init(&_member->flying_q, d_env.wb_flush_size);
	q_init(&_member->blocked_q, d_env.wb_flush_size);
	//q_init(&_member->wb_cmt_load_q, d_env.wb_flush_size);
	q_init(&_member->wb_master_q, d_env.wb_flush_size);
	q_init(&_member->wb_retry_q, d_env.wb_flush_size);

	struct flush_list *fl = (struct flush_list *)malloc(sizeof(struct flush_list));
	fl->size = 0;
	fl->list = (struct flush_node *)calloc(d_env.wb_flush_size, sizeof(struct flush_node));
	_member->flush_list = fl;

#ifdef HASH_KVSSD
	_member->max_try = 0;
#endif

	_member->hash_table = d_htable_init(d_env.wb_flush_size * 2);

	return 0;
}

static void demand_stat_init(struct demand_stat *const _stat) {

}

uint32_t demand_create(lower_info *li, blockmanager *bm, algorithm *algo){

	/* map modules */
	algo->li = li;
	algo->bm = bm;

	/* init env */
	demand_env_init(&d_env);
	/* init member */
	demand_member_init(&d_member);
	/* init stat */
	demand_stat_init(&d_stat);

	d_cache = select_cache((cache_t)d_env.cache_id);

	/* create() for range query */
	range_create();

	/* create() for page allocation module */
	page_create(bm);

#ifdef DVALUE
	/* create() for grain functions */
	grain_create();
#endif

	return 0;
}

static int count_filled_entry() {
	int ret = 0;
	for (int i = 0; i < d_cache->env.nr_valid_tpages; i++) {
		struct pt_struct *pt = d_cache->member.mem_table[i];
		for (int j = 0; j < EPP; j++) {
			if (pt[j].ppa != UINT32_MAX) {
				ret++;
			}
		}
	}
	return ret;
}

static void print_hash_collision_cdf(uint64_t *hc) {
	int total = 0;
	for (int i = 0; i < MAX_HASH_COLLISION; i++) {
		total += hc[i];
	}
	float _cdf = 0;
	for (int i = 0; i < MAX_HASH_COLLISION; i++) {
		if (hc[i]) {
			_cdf += (float)hc[i]/total;
			printf("%d,%ld,%.6f\n", i, hc[i], _cdf);
		}
	}
}

static void print_demand_stat(struct demand_stat *const _stat) {
	/* device traffic */
	puts("================");
	puts(" Device Traffic ");
	puts("================");

	printf("Data_Read:  \t%ld\n", _stat->data_r);
	printf("Data_Write: \t%ld\n", _stat->data_w);
	puts("");
	printf("Trans_Read: \t%ld\n", _stat->trans_r);
	printf("Trans_Write:\t%ld\n", _stat->trans_w);
	puts("");
	printf("DataGC cnt: \t%ld\n", _stat->dgc_cnt);
	printf("DataGC_DR:  \t%ld\n", _stat->data_r_dgc);
	printf("DataGC_DW:  \t%ld\n", _stat->data_w_dgc);
	printf("DataGC_TR:  \t%ld\n", _stat->trans_r_dgc);
	printf("DataGC_TW:  \t%ld\n", _stat->trans_w_dgc);
	puts("");
	printf("TransGC cnt:\t%ld\n", _stat->tgc_cnt);
	printf("TransGC_TR: \t%ld\n", _stat->trans_r_tgc);
	printf("TransGC_TW: \t%ld\n", _stat->trans_w_tgc);
	puts("");

	int amplified_read = _stat->trans_r + _stat->data_r_dgc + _stat->trans_r_dgc + _stat->trans_r_tgc;
	int amplified_write = _stat->trans_w + _stat->data_w_dgc + _stat->trans_w_dgc + _stat->trans_w_tgc;
	printf("RAF: %.2f\n", (float)(_stat->data_r + amplified_read)/_stat->data_r);
	printf("WAF: %.2f\n", (float)(_stat->data_w + amplified_write)/_stat->data_w);
	puts("");

	/* r/w specific traffic */
	puts("==============");
	puts(" R/W analysis ");
	puts("==============");

	puts("[Read]");
	printf("*Read Reqs: \t%ld\n", _stat->read_req_cnt);
	printf("Data read:  \t%ld (+%ld Write-buffer hits)\n", _stat->d_read_on_read, _stat->wb_hit);
	printf("Data write: \t%ld\n", _stat->d_write_on_read);
	printf("Trans read: \t%ld\n", _stat->t_read_on_read);
	printf("Trans write:\t%ld\n", _stat->t_write_on_read);
	puts("");

	puts("[Write]");
	printf("*Write Reqs:\t%ld\n", _stat->write_req_cnt);
	printf("Data read:  \t%ld\n", _stat->d_read_on_write);
	printf("Data write: \t%ld\n", _stat->d_write_on_write);
	printf("Trans read: \t%ld\n", _stat->t_read_on_write);
	printf("Trans write:\t%ld\n", _stat->t_write_on_write);
	puts("");

	/* write buffer */
	puts("==============");
	puts(" Write Buffer ");
	puts("==============");

	printf("Write-buffer Hit cnt: %ld\n", _stat->wb_hit);
	puts("");


#ifdef HASH_KVSSD
	puts("================");
	puts(" Hash Collision ");
	puts("================");

	puts("[Overall Hash-table Load Factor]");
	int filled_entry_cnt = count_filled_entry();
	int total_entry_cnt = d_cache->env.nr_valid_tentries;
	printf("Total entry:  %d\n", total_entry_cnt);
	printf("Filled entry: %d\n", filled_entry_cnt);
	printf("Load factor:  %.2f%%\n", (float)filled_entry_cnt/total_entry_cnt*100);
	puts("");

	puts("[write(insertion)]");
	print_hash_collision_cdf(_stat->w_hash_collision_cnt);

	puts("[read]");
	print_hash_collision_cdf(_stat->r_hash_collision_cnt);
	puts("");

	puts("=======================");
	puts(" Fingerprint Collision ");
	puts("=======================");

	puts("[Read]");
	printf("fp_match:     %ld\n", _stat->fp_match_r);
	printf("fp_collision: %ld\n", _stat->fp_collision_r);
	printf("rate: %.2f\n", (float)_stat->fp_collision_r/(_stat->fp_match_r+_stat->fp_collision_r)*100);
	puts("");

	puts("[Write]");
	printf("fp_match:     %ld\n", _stat->fp_match_w);
	printf("fp_collision: %ld\n", _stat->fp_collision_w);
	printf("rate: %.2f\n", (float)_stat->fp_collision_w/(_stat->fp_match_w+_stat->fp_collision_w)*100);
	puts("");
#endif
}

static void demand_member_free(struct demand_member *const _member) {
/*	for (int i = 0; i < d_env.nr_valid_tpages; i++) {
		q_free(_member->cmt[i]->blocked_q);
		q_free(_member->cmt[i]->wait_q);
		free(_member->cmt[i]);
	}
	free(_member->cmt);

	for(int i=0;i<d_env.nr_valid_tpages;i++) {
		free(_member->mem_table[i]);
	}
	free(_member->mem_table);

	lru_free(_member->lru); */
	skiplist_free(_member->write_buffer);

	q_free(_member->flying_q);
	q_free(_member->blocked_q);
	//q_free(_member->wb_cmt_load_q);
	q_free(_member->wb_master_q);
	q_free(_member->wb_retry_q);

#ifdef PART_CACHE
	q_free(&_member->wait_q);
	q_free(&_member->write_q);
	q_free(&_member->flying_q);
#endif
}

void demand_destroy(lower_info *li, algorithm *algo){

	/* print stat */
	print_demand_stat(&d_stat);

	/* free member */
	demand_member_free(&d_member);

	/* cleanup cache */
	d_cache->destroy();
}

#ifdef HASH_KVSSD
static uint32_t hashing_key(char* key,uint8_t len) {
	char* string;
	Sha256Context ctx;
	SHA256_HASH hash;
	int bytes_arr[8];
	uint32_t hashkey;

	string = key;

	Sha256Initialise(&ctx);
	Sha256Update(&ctx, (unsigned char*)string, len);
	Sha256Finalise(&ctx, &hash);

	for(int i=0; i<8; i++) {
		bytes_arr[i] = ((hash.bytes[i*4] << 24) | (hash.bytes[i*4+1] << 16) | \
				(hash.bytes[i*4+2] << 8) | (hash.bytes[i*4+3]));
	}

	hashkey = bytes_arr[0];
	for(int i=1; i<8; i++) {
		hashkey ^= bytes_arr[i];
	}

	return hashkey;
}

static uint32_t hashing_key_fp(char* key,uint8_t len) {
	char* string;
	Sha256Context ctx;
	SHA256_HASH hash;
	int bytes_arr[8];
	uint32_t hashkey;

	string = key;

	Sha256Initialise(&ctx);
	Sha256Update(&ctx, (unsigned char*)string, len);
	Sha256Finalise(&ctx, &hash);

	for(int i=0; i<8; i++) {
		bytes_arr[i] = ((hash.bytes[i*4]) | (hash.bytes[i*4+1] << 8) | \
				(hash.bytes[i*4+2] << 16) | (hash.bytes[i*4+3] << 24));
	}

	hashkey = bytes_arr[0];
	for(int i=1; i<8; i++) {
		hashkey ^= bytes_arr[i];
	}

	return (hashkey & ((1<<FP_SIZE)-1));
}

static struct hash_params *make_hash_params(request *const req) {
	struct hash_params *h_params = (struct hash_params *)malloc(sizeof(struct hash_params));
	h_params->hash = hashing_key(req->key.key, req->key.len);
#ifdef STORE_KEY_FP
	h_params->key_fp = hashing_key_fp(req->key.key, req->key.len);
#endif
	h_params->cnt = 0;
	h_params->find = HASH_KEY_INITIAL;
	h_params->lpa = 0;

	return h_params;
}
#endif

uint32_t demand_read(request *const req){
	uint32_t rc;
	pthread_mutex_lock(&d_member.op_lock);
#ifdef HASH_KVSSD
	if (!req->hash_params) {
		d_stat.read_req_cnt++;
		req->hash_params = (void *)make_hash_params(req);
	}
#endif
	rc = __demand_read(req);
	if (rc == UINT32_MAX) {
		req->type = FS_NOTFOUND_T;
		req->end_req(req);
	}
	pthread_mutex_unlock(&d_member.op_lock);
	return 0;
}

uint32_t demand_write(request *const req) {
	uint32_t rc;
	pthread_mutex_lock(&d_member.op_lock);
#ifdef HASH_KVSSD
	if (!req->hash_params) {
		d_stat.write_req_cnt++;
		req->hash_params = (void *)make_hash_params(req);
	}
#endif
	rc = __demand_write(req);
	pthread_mutex_unlock(&d_member.op_lock);
	return rc;
}

uint32_t demand_remove(request *const req) {
	int rc;
	rc = __demand_remove(req);
	req->end_req(req);
	return 0;
}

