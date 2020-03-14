#ifndef __LSM_HEADER__
#define __LSM_HEADER__
#include <pthread.h>
#include <limits.h>
#include "level.h"
#include "skiplist.h"
#include "bloomfilter.h"
#include "cache.h"
#include "level.h"
#include "page.h"
#include "../../include/settings.h"
#include "../../include/utils/rwlock.h"
#include "../../interface/queue.h"
#include "../../include/container.h"
#include "../../include/settings.h"
#include "../../include/lsm_settings.h"
#include "../../include/utils/dl_sync.h"
#include "../../include/types.h"
#include "../../include/sem_lock.h"
#include "../../include/data_struct/redblack.h"
#include "../../interface/interface.h"
//#define ONESEGMENT (DEFKEYINHEADER*DEFVALUESIZE)
#ifdef DVALUE
	#define CONVPPA(_ppa) _ppa/NPCINPAGE
#else
	#define CONVPPA(_ppa) _ppa
#endif


#define HEADERR MAPPINGR
#define HEADERW MAPPINGW
#define GCHR GCMR
#define GCHW GCMW
#define SDATAR 9
#define RANGER 10
#define OLDDATA 13
#define BGREAD 15
#define BGWRITE 16
#define TESTREAD 17

//lower type, algo type
typedef struct level level;
typedef struct run run_t;
typedef struct level_ops level_ops;
typedef struct htable htable;

enum READTYPE{
	NOTFOUND,FOUND,CACHING,FLYING
};

enum LSMTYPE{
	PINNING,FILTERING,CACHE,FILTERCACHE,ALLMIXED
};

typedef struct lsm_params{
	//dl_sync lock;
	uint8_t lsm_type;
	ppa_t ppa;
	void *entry_ptr;
	PTR test;
	PTR* target;
	value_set* value;
	htable * htable_ptr;
	fdriver_lock_t *lock;
}lsm_params;

typedef struct lsm_sub_req{
	KEYT key;
	value_set *value;
	request *parents;
	run_t *ent;
	uint8_t status;
}lsm_sub_req;

typedef struct lsm_range_params{
	uint8_t lsm_type;
	int now;
	int not_found;
	int max;
	uint32_t now_level;
	uint8_t status;
	char **mapping_data;
	fdriver_lock_t global_lock;
	lsm_sub_req *children;
}lsm_range_params;

enum comp_opt_type{
	NON,PIPE,HW,MIXEDCOMP
};

typedef struct lsmtree{
	uint32_t KEYNUM;
	uint32_t FLUSHNUM;
	uint32_t LEVELN;
	uint32_t LEVELCACHING;
	uint32_t VALUESIZE;
	uint32_t ONESEGMENT;
	float caching_size;

	bool nocpy;
	bool gc_opt;
	bool multi_level_comp;
	uint8_t comp_opt;
	uint8_t lsm_type;

	bool inplace_compaction;
	bool delayed_header_trim;
	bool gc_started;
	bool hw_read;
	//this is for nocpy, when header_gc triggered in compactioning
	uint32_t delayed_trim_ppa;//UINT_MAX is nothing to do

	uint32_t keynum_in_header;
	uint32_t keynum_in_header_cnt;
	float size_factor;
	float last_size_factor;
	uint32_t result_padding;
	bool* size_factor_change;//true: it will be changed size

	double avg_of_length;
	uint32_t length_cnt;
	uint32_t added_header;

	bool debug_flag;

	level **disk;
	level *c_level;
	level_ops *lop;

	pthread_mutex_t memlock;
	pthread_mutex_t templock;

	pthread_mutex_t *level_lock;
	PTR caching_value;

	struct skiplist *memtable;
	struct skiplist *temptable;

	struct skiplist *gc_list;
	bool gc_compaction_flag;

	struct queue *re_q;
	struct queue *gc_q;

	struct cache* lsm_cache;
	lower_info* li;
	blockmanager *bm;

	uint32_t last_level_comp_term; //for avg header
	uint32_t check_cnt;
	uint32_t needed_valid_page;
	uint32_t target_gc_page;
#ifdef EMULATOR
	Redblack rb_ppa_key;
#endif

#ifdef DVALUE
	/*data caching*/
	pthread_mutex_t data_lock;
	ppa_t data_ppa;
	value_set* data_value;
#endif

	/*bench info!*/
	uint32_t data_gc_cnt;
	uint32_t header_gc_cnt;
	uint32_t compaction_cnt;
	uint32_t zero_compaction_cnt;

	__block* t_block;
	struct lsm_block *active_block;
}lsmtree;

uint32_t lsm_argument_set(int argc, char **argv);
uint32_t lsm_create(lower_info *, blockmanager *, algorithm *);
uint32_t __lsm_create_normal(lower_info *, algorithm *);
//uint32_t __lsm_create_simulation(lower_info *, algorithm*);
void lsm_destroy(lower_info*, algorithm*);
uint32_t lsm_get(request *const);
uint32_t lsm_set(request *const);
uint32_t lsm_multi_get(request *const, int num);
uint32_t lsm_proc_re_q();
uint32_t lsm_remove(request *const);

uint32_t __lsm_get(request *const);
uint8_t lsm_find_run(KEYT key, run_t **,struct keyset **, int *level, int *run);
uint32_t __lsm_range_get(request *const);

void* lsm_end_req(struct algo_req*const);
void* lsm_mget_end_req(struct algo_req *const);
bool lsm_kv_validcheck(uint8_t *, int idx);
void lsm_kv_validset(uint8_t *,int idx);

htable *htable_copy(htable *);
htable *htable_assign(char*,bool);
htable *htable_dummy_assign();
void htable_free(htable*);
void htable_print(htable*,ppa_t);
algo_req *lsm_get_req_factory(request*,uint8_t);
void htable_check(htable *in,KEYT lpa,ppa_t ppa,char *);

uint32_t lsm_multi_set(request *const, uint32_t num);
uint32_t lsm_range_get(request *const);
uint32_t lsm_memory_size();
uint32_t lsm_simul_put(ppa_t ppa, KEYT key);
//copy the value
uint32_t lsm_test_read(ppa_t ppa, char *data);
level *lsm_level_resizing(level *target, level *src);
KEYT* lsm_simul_get(ppa_t ppa); //copy the value
void lsm_simul_del(ppa_t ppa);
#endif

/*
void lsm_save(lsmtree *);
void lsm_trim_set(value_set* ,uint8_t *);
uint8_t *lsm_trim_get(PTR);
lsmtree* lsm_load();*/
