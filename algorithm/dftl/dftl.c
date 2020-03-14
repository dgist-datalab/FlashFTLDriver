#include "dftl.h"
#include "../../bench/bench.h"

/* DFTL methods */
algorithm __demand = {
    .create  = demand_create,
    .destroy = demand_destroy,
    .read    = demand_get,
    .write   = demand_set,
    .remove  = demand_remove
};

/* Modules */
LRU *lru;        // LRU list
queue *dftl_q;   // Retry queue for algorithm(dftl) layer
BM_T *bm;        // Block manager (Wrapping module)
b_queue *free_b; // Free block list
Heap *data_b;    // (Allocated) Data block heap
Heap *trans_b;   // (Allocated) Trans block heap

/* Tables */
C_TABLE *CMT;          // Cached Mapping Table
mem_table* mem_arr;    // Memtable of translation pages (D_TABLE)
D_OOB *demand_OOB;     // Page OOB
struct prefetch_struct *ppa_prefetch;    // Prefetched ppa table
request **waiting_arr; // Array of blocked requests due to lack of cache entry

/* DFTL variables */
int32_t num_page;
int32_t num_block;
int32_t p_p_b;
int32_t num_tpage;
int32_t num_tblock;
int32_t num_dpage;
int32_t num_dblock;

int32_t max_cache_entry; // # of entire translation pages
int32_t num_max_cache;   // Maximum # of cached translation pages on certain configuration
int32_t num_caching;     // Current # of cached translation pages (or dirty page on C_CACHE)
int32_t real_max_cache;  // Duplicate of 'max_cache_entry'
uint64_t max_write_buf;  // Write buffer size

int32_t num_flying; // # of flying mapping requests(read/write)
uint32_t ppa_idx;   // Index of prefetched ppa table
int32_t waiting;    // # of waiting requests on 'waiting_arr'

#if W_BUFF
skiplist *write_buffer;
int32_t buf_hit;
#endif

#if C_CACHE
LRU *c_lru;
int32_t num_clean; // # of clean translation page on cache
int32_t max_clean_cache;
int32_t max_dirty_cache;
#endif

/* GC variables */
Block *t_reserved; // pointer of reserved block for translation gc
Block *d_reserved; // pointer of reserved block for data gc
volatile int32_t trans_gc_poll;
volatile int32_t data_gc_poll;

/* Statistic variables */
int32_t tgc_count;
int32_t dgc_count;
int32_t tgc_w_dgc_count;
int32_t read_tgc_count;
int32_t evict_count;

int32_t data_r;
int32_t trig_data_r;
int32_t data_w;
int32_t trans_r;
int32_t trans_w;
int32_t dgc_r;
int32_t dgc_w;
int32_t tgc_r;
int32_t tgc_w;

int32_t cache_hit_on_read;
int32_t cache_miss_on_read;
int32_t cache_hit_on_write;
int32_t cache_miss_on_write;

int32_t clean_hit_on_read;
int32_t dirty_hit_on_read;
int32_t clean_hit_on_write;
int32_t dirty_hit_on_write;

int32_t clean_eviction;
int32_t dirty_eviction;

int32_t clean_evict_on_read;
int32_t clean_evict_on_write;
int32_t dirty_evict_on_read;
int32_t dirty_evict_on_write;

static void print_algo_log() {
	printf("\n");
#if C_CACHE
	printf(" |---------- algorithm_log : CtoC Demand FTL\n");
#else
	if (num_max_cache == max_cache_entry) {
		printf(" |---------- algorithm_log : Page FTL\n");
	} else {
		printf(" |---------- algorithm_log : Demand FTL\n");
	}

#endif
	printf(" | Total Blocks(Segments): %d\n", num_block); 
	printf(" |  -Translation Blocks:   %d (+1 reserved)\n", num_tblock);
	printf(" |  -Data Blocks:          %d (+1 reserved)\n", num_dblock);
	printf(" | Total Pages:            %d\n", num_page);
	printf(" |  -Translation Pages:    %d\n", num_tpage);
	printf(" |  -Data Pages            %d\n", num_dpage);
	printf(" |  -Page per Block:       %d\n", p_p_b);
	printf(" | Total cache entries:    %d\n", max_cache_entry);
#if C_CACHE
	printf(" |  -Clean Cache entries:  %d\n", num_max_cache);
	printf(" |  -Dirty Cache entries:  %d\n", max_clean_cache);
#else
	printf(" |  -Mixed Cache entries:  %d\n", num_max_cache);
#endif
	printf(" |  -Cache Percentage:     %0.3f%%\n", (float)real_max_cache/max_cache_entry*100);
	printf(" | Write buffer size:      %d\n", max_write_buf);
	printf(" |\n");
	printf(" | ! Assume no Shadow buffer\n");
	printf(" | ! PPAs are prefetched on write flush stage\n");
	printf(" |---------- algorithm_log END\n\n");
}


uint32_t demand_create(lower_info *li, algorithm *algo){
    /* Initialize pre-defined values by using macro */
    num_page        = _NOP;
    num_block       = _NOS;
    p_p_b           = _PPS;
    num_tblock      = ((num_block / EPP) + ((num_block % EPP != 0) ? 1 : 0)) * 8;
    num_tpage       = num_tblock * p_p_b;
    num_dblock      = num_block - num_tblock - 2;
    num_dpage       = num_dblock * p_p_b;
    max_cache_entry = (num_page / EPP) + ((num_page % EPP != 0) ? 1 : 0);


    /* Cache control & Init */
	//num_max_cache = max_cache_entry; // max cache
    //num_max_cache = 1; // 1 cache
	//num_max_cache = max_cache_entry / 4; // 1/4 cache
    //num_max_cache = max_cache_entry / 20; // 5%
    num_max_cache = max_cache_entry / 10; // 10%
    //num_max_cache = max_cache_entry / 8; // 12.5%
    //num_max_cache = max_cache_entry / 40; // 2.5%
	//num_max_cache = max_cache_entry / 50; // 2%

    real_max_cache = num_max_cache;

    num_caching = 0;
    //max_write_buf = 512;
	max_write_buf = 1024;
//	max_write_buf = 1;
#if C_CACHE
    max_clean_cache = num_max_cache / 2; // 50 : 50
    num_max_cache -= max_clean_cache;

    num_clean = 0;
#endif

    /* Print information */
	print_algo_log();

    /* Map lower info */
    algo->li = li;


    /* Module Init */
    lru_init(&lru);
#if C_CACHE
    lru_init(&c_lru);
#endif

    q_init(&dftl_q, 1024);

    bm = BM_Init(num_block, p_p_b, 2, 1); // 2 heaps and 1 queue
    BM_Queue_Init(&free_b);
    for(int i = 0; i < num_block - 2; i++){
        BM_Enqueue(free_b, &bm->barray[i]);
    }
    t_reserved = &bm->barray[num_block - 2];
    d_reserved = &bm->barray[num_block - 1];
    data_b = BM_Heap_Init(num_dblock);
    trans_b = BM_Heap_Init(num_tblock);
    bm->harray[0] = data_b;
    bm->harray[1] = trans_b;
    bm->qarray[0] = free_b;

#if W_BUFF
    write_buffer = skiplist_init();
#endif


    /* Table allocation & Init */
    CMT = (C_TABLE*)malloc(sizeof(C_TABLE) * max_cache_entry);
    mem_arr = (mem_table *)malloc(sizeof(mem_table) * max_cache_entry);
    demand_OOB = (D_OOB*)malloc(sizeof(D_OOB) * num_page);
    ppa_prefetch = (struct prefetch_struct *)malloc(sizeof(struct prefetch_struct) * max_write_buf);
    waiting_arr = (request **)malloc(sizeof(request *) * max_write_buf);

    for(int i = 0; i < max_cache_entry; i++){
        CMT[i].t_ppa = -1;
        CMT[i].idx = i;
        CMT[i].p_table = NULL;
        CMT[i].queue_ptr = NULL;
#if C_CACHE
        CMT[i].clean_ptr = NULL;
#endif
        CMT[i].state = CLEAN;
        CMT[i].flying = false;
        CMT[i].flying_arr = (request **)malloc(sizeof(request *) * 1024);
        CMT[i].num_waiting = 0;
        CMT[i].read_hit = 0;
        CMT[i].write_hit = 0;
    }

    for (int i = 0; i < max_cache_entry; i++) {
        mem_arr[i].mem_p = (D_TABLE *)malloc(PAGESIZE);
        memset(mem_arr[i].mem_p, -1, PAGESIZE);
    }

    memset(demand_OOB, -1, sizeof(D_OOB) * num_page);

    for (size_t i = 0; i < max_write_buf; i++) {
        ppa_prefetch[i].ppa = dp_alloc();
        ppa_prefetch[i].sn = NULL;
    }

    return 0;
}

void demand_destroy(lower_info *li, algorithm *algo){

    /*
    puts("");
    for (int i = 0; i < max_cache_entry; i++) {
        if (CMT[i].read_hit || CMT[i].write_hit) {
            printf("CMT[%d]: read(%u) / write(%u)\n", i, CMT[i].read_hit, CMT[i].write_hit);
        }
    }*/


    /* Print information */
    printf("# of gc: %d\n", tgc_count + dgc_count);
    printf("# of translation page gc: %d\n", tgc_count);
    printf("# of data page gc: %d\n", dgc_count);
    printf("# of translation page gc w/ data page gc: %d\n", tgc_w_dgc_count);
    printf("# of translation page gc w/ read op: %d\n", read_tgc_count);
    printf("# of evict: %d\n", evict_count);
#if W_BUFF
    printf("# of buf hit: %d\n", buf_hit);
    skiplist_free(write_buffer);
#endif
    printf("!!! print info !!!\n");
    printf("BH: buffer hit, H: hit, R: read, MC: memcpy, CE: clean eviction, DE: dirty eviction, GC: garbage collection\n");
    printf("a_type--->>> 0: BH, 1: H\n");
    printf("2: R & MC, 3: R & CE & MC\n");
    printf("4: R & DE & MC, 5: R & CE & GC & MC\n");
    printf("6: R & DE & GC & MC\n");
    printf("!!! print info !!!\n");
    printf("Cache hit on read: %d\n", cache_hit_on_read);
    printf("Cache miss on read: %d\n", cache_miss_on_read);
    printf("Cache hit on write: %d\n", cache_hit_on_write);
    printf("Cache miss on write: %d\n\n", cache_miss_on_write);

    printf("Miss ratio: %.2f%%\n", (float)(cache_miss_on_read+cache_miss_on_write)/(data_r*2) * 100);
    printf("Miss ratio on read : %.2f%%\n", (float)(cache_miss_on_read)/(data_r) * 100);
    printf("Miss ratio on write: %.2f%%\n\n", (float)(cache_miss_on_write)/(data_r) * 100);

    printf("Clean hit on read: %d\n", clean_hit_on_read);
    printf("Dirty hit on read: %d\n", dirty_hit_on_read);
    printf("Clean hit on write: %d\n", clean_hit_on_write);
    printf("Dirty hit on write: %d\n\n", dirty_hit_on_write);

    printf("# Clean eviction: %d\n", clean_eviction);
    printf("# Dirty eviction: %d\n\n", dirty_eviction);

    printf("Clean eviciton on read: %d\n", clean_evict_on_read);
    printf("Dirty eviction on read: %d\n", dirty_evict_on_read);
    printf("Clean eviction on write: %d\n", clean_evict_on_write);
    printf("Dirty eviction on write: %d\n\n", dirty_evict_on_write);

    printf("Dirty eviction ratio: %.2f%%\n", 100*((float)dirty_eviction/(clean_eviction+dirty_eviction)));
    printf("Dirty eviction ratio on read: %.2f%%\n", 100*((float)dirty_evict_on_read/(clean_evict_on_read+dirty_evict_on_read)));
    printf("Dirty eviction ratio on write: %.2f%%\n\n", 100*((float)dirty_evict_on_write/(clean_evict_on_write+dirty_evict_on_write)));

    printf("WAF: %.2f\n\n", (float)(data_r+dirty_evict_on_write)/data_r);

    printf("\nnum caching: %d\n", num_caching);
    printf("num_flying: %d\n\n", num_flying);

    /* Clear modules */
    q_free(dftl_q);
    BM_Free(bm);

    lru_free(lru);
#if C_CACHE
    lru_free(c_lru);
#endif

    /* Clear tables */
    free(waiting_arr);
    free(ppa_prefetch);
    free(demand_OOB);

    for (int i = 0; i < max_cache_entry; i++) {
        free(mem_arr[i].mem_p);
    }
    free(mem_arr);
    free(CMT);
}

static uint32_t demand_cache_update(request *const req, char req_t) {
    int lpa = req->key;
    C_TABLE *c_table = &CMT[D_IDX];
    int32_t t_ppa = c_table->t_ppa;

    if (req_t == 'R') {
#if C_CACHE
        if (c_table->clean_ptr) {
            lru_update(c_lru, c_table->clean_ptr);
        }
        if (c_table->queue_tpr) {
            lru_update(lru, c_table->queue_ptr);
        }
#else
        lru_update(lru, c_table->queue_ptr);
        if (c_table->state == CLEAN) {
            clean_hit_on_read++;
        } else {
            dirty_hit_on_read++;
        }
#endif
    } else {
#if C_CACHE
        if (c_table->state == CLEAN) { // Clean hit
            c_table->state = DIRTY;
            BM_InvalidatePage(bm, t_ppa);

            // this page is dirty after hit, but still lies on clean lru
            lru_update(c_lru, c_table->clean_ptr);

            // migrate(copy) the lru element
            if (num_caching == num_max_cache) {
                demand_eviction(req, 'W', &gc_flag, &d_flag);
            }
            c_table->queue_ptr = lru_push(lru, (void *)c_table);
            num_caching++;

        } else { // Dirty hit
            if (c_table->clean_ptr) {
                lru_update(c_lru, c_table->clean_ptr);
            }
            lru_update(lru, c_table->queue_ptr);
        }
#else
        if (c_table->state == CLEAN) {
            c_table->state = DIRTY;
            BM_InvalidatePage(bm, t_ppa);
            clean_hit_on_write++;
        } else {
            dirty_hit_on_write++;
        }
        lru_update(lru, c_table->queue_ptr);
#endif
    }

    return 0;
}

static uint32_t demand_cache_eviction(request *const req, char req_t) {
    int lpa = req->key;
    C_TABLE *c_table = &CMT[D_IDX];
    int32_t t_ppa = c_table->t_ppa;

    bool gc_flag;
    bool d_flag;

    value_set *dummy_vs;
    algo_req *temp_req;

    read_params *checker;

    gc_flag = false;
    d_flag = false;

    // Reserve requests that share flying mapping table
    if (c_table->flying) {
        c_table->flying_arr[c_table->num_waiting++] = req;
        bench_algo_end(req);
        return 1;
    }

    if (num_flying == num_max_cache) { // This case occurs only if (QDEPTH > num_max_cache)
        waiting_arr[waiting++] = req;
        bench_algo_end(req);
        return 1;
    }

    checker = (read_params *)malloc(sizeof(read_params));
    checker->read = 0;
    checker->t_ppa = t_ppa;
    req->params = (void *)checker;

    if (req_t == 'R') {
		cache_miss_on_read++;

        req->type_ftl += 2;
#if C_CACHE
        if (num_clean == max_clean_cache) {
            req->type_ftl += 1;
            demand_eviction(req, 'R', &gc_flag, &d_flag);
            if(d_flag) req->type_ftl += 1;
            if(gc_flag) req->type_ftl += 2;
        }
#else
        if (num_caching + num_flying == num_max_cache) {
            req->type_ftl += 1;
            if (demand_eviction(req, 'R', &gc_flag, &d_flag) == 0) {
                c_table->flying = true;
                num_flying++;
                if(d_flag) req->type_ftl += 1;
                if(gc_flag) req->type_ftl += 2;
                bench_algo_end(req);
                return 1;
            }
            if(gc_flag) req->type_ftl += 2;
        }
#endif
    } else {
		cache_miss_on_write++;

        if (num_caching + num_flying == num_max_cache) {
            if (demand_eviction(req, 'W', &gc_flag, &d_flag) == 0) {
                c_table->flying = true;
                num_flying++;
                bench_algo_end(req);
                return 1;
            }
        }
    }

    if (t_ppa != -1) {
        c_table->flying = true;
        num_flying++;

        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_R, dummy_vs, req);

        bench_algo_end(req);
        __demand.li->read(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);

        return 1;
    }

    // Case of initial state (t_ppa == -1)
    // Read(get) method never enter here
    c_table->p_table   = mem_arr[D_IDX].mem_p;
    c_table->queue_ptr = lru_push(lru, (void*)c_table);
    c_table->state     = DIRTY;

    num_caching++;

    return 0;
}

static uint32_t demand_write_flying(request *const req, char req_t) {
    int lpa = req->key;
    C_TABLE *c_table = &CMT[D_IDX];
    int32_t t_ppa    = c_table->t_ppa;

    value_set *dummy_vs;
    algo_req *temp_req;

    if(t_ppa != -1) {
        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_R, dummy_vs, req);

        bench_algo_end(req);
        __demand.li->read(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);

        return 1;

    } else {
        if (req_t == 'R') {
            // not found
            printf("\nUnknown behavior: in demand_write_flying()\n");
        } else {
            /* Case of initial state (t_ppa == -1) */
            c_table->p_table   = mem_arr[D_IDX].mem_p;
            c_table->queue_ptr = lru_push(lru, (void*)c_table);
            c_table->state     = DIRTY;
            num_caching++;

            // Register reserved requests
            for (int i = 0; i < c_table->num_waiting; i++) {
                if (!inf_assign_try(c_table->flying_arr[i])) {
                    puts("not queued 3");
                    q_enqueue((void *)c_table->flying_arr[i], dftl_q);
                }
            }
            c_table->num_waiting = 0;
            c_table->flying = false;
            num_flying--;
            for (int i = 0; i < waiting; i++) {
                if (!inf_assign_try(waiting_arr[i])) {
                    puts("not queued 5");
                    q_enqueue((void *)waiting_arr[i], dftl_q);
                }
            }
            waiting = 0;
        }
    }

    return 0;
}

static uint32_t demand_read_flying(request *const req, char req_t) {
    int lpa = req->key;
    C_TABLE *c_table = &CMT[D_IDX];
    int32_t t_ppa = c_table->t_ppa;
    read_params *params = (read_params *)req->params;

    value_set *dummy_vs;
    algo_req *temp_req;

    // GC can occur while flying (t_ppa can be changed)
    if (params->t_ppa != t_ppa) {
        params->read  = 0;
        params->t_ppa = t_ppa;

        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_R, dummy_vs, req);

        bench_algo_end(req);
        __demand.li->read(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);

        return 1;
    }

    c_table->p_table = mem_arr[D_IDX].mem_p;
    num_caching++;

    if (req_t == 'R') {
#if C_CACHE
        c_table->clean_ptr = lru_push(c_lru, (void *)c_table);
#else
        c_table->queue_ptr = lru_push(lru, (void*)c_table);
#endif
    } else {
        c_table->queue_ptr = lru_push(lru, (void*)c_table);
        c_table->state     = DIRTY;
        BM_InvalidatePage(bm, t_ppa);
    }

    // Register reserved requests
    for (int i = 0; i < c_table->num_waiting; i++) {
        //while (!inf_assign_try(c_table->flying_arr[i])) {}
        if (!inf_assign_try(c_table->flying_arr[i])) {
            puts("not queued 4");
            q_enqueue((void *)c_table->flying_arr[i], dftl_q);
        }
    }
    c_table->num_waiting = 0;
    c_table->flying = false;
    num_flying--;
    for (int i = 0; i < waiting; i++) {
        if (!inf_assign_try(waiting_arr[i])) {
            puts("not queued 5");
            q_enqueue((void *)waiting_arr[i], dftl_q);
        }
    }
    waiting = 0;

    return 0;
}


static uint32_t __demand_get(request *const req){
    int32_t lpa; // Logical data page address
    int32_t ppa; // Physical data page address
    int32_t t_ppa; // Translation page address
    C_TABLE* c_table; // Cache mapping entry pointer
    D_TABLE *p_table; // pointer of p_table on cme
#if W_BUFF
    snode *temp;
#endif

    bench_algo_start(req);
    lpa = req->key;
    if(lpa > RANGE + 1){ // range check
        printf("range error %d\n",lpa);
        exit(3);
    }

#if W_BUFF
    /* Check skiplist first */
    if((temp = skiplist_find(write_buffer, lpa))){
        buf_hit++;
        memcpy(req->value->value, temp->value->value, PAGESIZE);
        req->type_ftl = 0;
        req->type_lower = 0;
        bench_algo_end(req);
        req->end_req(req);
        return 1;
    }
#endif
    /* Assign values from cache table */
    c_table = &CMT[D_IDX];
    p_table = c_table->p_table;
    t_ppa   = c_table->t_ppa;

    if (req->params == NULL) {
        if (p_table) { // Cache hit
            ppa = p_table[P_IDX].ppa;
            if (ppa == -1) {
                bench_algo_end(req);
                return UINT32_MAX;
            }
            cache_hit_on_read++;
            // Cache update
            demand_cache_update(req, 'R');
            req->type_ftl += 1;

        } else { // Cache miss
            if (t_ppa == -1) {
                bench_algo_end(req);
                return UINT32_MAX;
            }
            //cache_miss_on_read++;
            if (demand_cache_eviction(req, 'R') == 1) {
                return 1;
            }
        }

    } else { // Flying request
        if (((read_params *)req->params)->read == 0) { // Case of mapping write finished
            if (demand_write_flying(req, 'R') == 1) {
                return 1;
            }

        } else { // Case of mapping read finished
            if (demand_read_flying(req, 'R') == 1) {
                return 1;
            }
        }
    }

    free(req->params);
    req->params = NULL;

    c_table->read_hit++;

    /* Get actual data from device */
    p_table = c_table->p_table;
    ppa = p_table[P_IDX].ppa;
    if (ppa == -1) {
        bench_algo_end(req);
        return UINT32_MAX;
    }
    bench_algo_end(req);
    // Get data in ppa
    __demand.li->read(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_R, NULL, req));

    return 1;
}

static uint32_t __demand_set(request *const req){
    /* !!! you need to print error message and exit program, when you set more valid
       data than number of data page !!! */
    int32_t lpa; // Logical data page address
    int32_t ppa; // Physical data page address
    C_TABLE *c_table; // Cache mapping entry pointer
    D_TABLE *p_table; // pointer of p_table on cme
    algo_req *my_req; // pseudo request pointer
#if W_BUFF
    snode *temp;
    sk_iter *iter;
#endif
	static bool is_flush = false;

    bench_algo_start(req);

    lpa = req->key;
    if(lpa > RANGE + 1){ // range check
        printf("range error %d\n",lpa);
        exit(3);
    }

    /* If the write buffer is already full, flush it */
    if (write_buffer->size == max_write_buf) {
        /* Push all the data to lower */
        for (size_t i = 0;i < max_write_buf; i++) {
            temp = ppa_prefetch[i].sn;

            /* Actual part of data push */
            /* Push the buffered data to pre-fetched ppa */
            my_req = assign_pseudo_req(DATA_W, temp->value, NULL);
            __demand.li->write(temp->ppa, PAGESIZE, temp->value, ASYNC, my_req);

            temp->value = NULL; // this memory area will be freed in end_req
        }

/*
        iter = skiplist_get_iterator(write_buffer);
        for (size_t i = 0;i < max_write_buf; i++) {
            temp = skiplist_get_next(iter);

            my_req = assign_pseudo_req(DATA_W, temp->value, NULL);
            __demand.li->write(temp->ppa, PAGESIZE, temp->value, ASYNC, my_req);

            temp->value = NULL; // this memory area will be freed in end_req
        }
*/

        // prefetch ppa to hide write overhead on read
        for (size_t i = 0; i < max_write_buf; i++) {
            ppa_prefetch[i].ppa = dp_alloc();
            ppa_prefetch[i].sn = NULL;
        }
        ppa_idx = 0;

        // Clear the skiplist
        //free(iter);
        skiplist_free(write_buffer);
        write_buffer = skiplist_init();

        // Wait until all flying requests(set) are finished
        __demand.li->lower_flying_req_wait();
		is_flush = true;
    }

    /* Insert data to skiplist (default) */
    lpa = req->key;
    c_table = &CMT[D_IDX];
    p_table = c_table->p_table;

    if (req->params == NULL) {
        if (p_table) {
            cache_hit_on_write++;
            demand_cache_update(req, 'W');

        } else {
            //cache_miss_on_write++;
            if (demand_cache_eviction(req, 'W') == 1) {
                return 1;
            }
        }
    } else {
        if (((read_params *)req->params)->read == 0) { // Case of mapping write finished
            if (demand_write_flying(req, 'W') == 1) {
                return 1;
            }

        } else { // Case of mapping read finished
            if (demand_read_flying(req, 'W') == 1) {
                return 1;
            }
        }
    }
    free(req->params);
    req->params = NULL;

    c_table->write_hit++;

    temp = skiplist_insert(write_buffer, lpa, req->value, true);

    // If the value is successfully inserted to write_buffer, then update the mapping table entry
    if (write_buffer->size == ppa_idx+1) {
        ppa = ppa_prefetch[ppa_idx].ppa;
        temp->ppa = ppa;

        ppa_prefetch[ppa_idx++].sn = temp;

        // if there is previous data with same lpa, then invalidate it
        p_table = c_table->p_table;
        if(p_table[P_IDX].ppa != -1){
            BM_InvalidatePage(bm, p_table[P_IDX].ppa);
        }

        // Update mapping table & OOB
        p_table[P_IDX].ppa = ppa;
        BM_ValidatePage(bm, ppa);
        demand_OOB[ppa].lpa = lpa;
    }
    req->value = NULL; // moved to 'value' field of snode
    bench_algo_end(req);
    req->end_req(req);

    return 1;
}

static uint32_t __demand_remove(request *const req) {
    int32_t lpa;
    int32_t ppa;
    int32_t t_ppa;
    C_TABLE *c_table;
    //value_set *p_table_vs;
    D_TABLE *p_table;
    bool gc_flag;
    bool d_flag;
    value_set *dummy_vs;

    //value_set *temp_value_set;
    algo_req *temp_req;
    demand_params *params;


    bench_algo_start(req);

    // Range check
    lpa = req->key;
    if (lpa > RANGE + 1) {
        printf("range error %d\n",lpa);
        exit(3);
    }

    c_table = &CMT[D_IDX];
    p_table = c_table->p_table;
    t_ppa   = c_table->t_ppa;

#if W_BUFF
    if (skiplist_delete(write_buffer, lpa) == 0) { // Deleted on skiplist
        bench_algo_end(req);
        return 0;
    }
#endif

    /* Get cache page from cache table */
    if (p_table) { // Cache hit
#if C_CACHE
        if (c_table->state == CLEAN) { // Clean hit
            lru_update(c_lru, c_table->clean_ptr);

        } else { // Dirty hit
            if (c_table->clean_ptr) {
                lru_update(c_lru, c_table->clean_ptr);
            }
            lru_update(lru, c_table->queue_ptr);
        }
#else
        lru_update(lru, c_table->queue_ptr);
#endif

    } else { // Cache miss

        // Validity check by t_ppa
        if (t_ppa == -1) {
            bench_algo_end(req);
            return UINT32_MAX;
        }

        if (num_caching == num_max_cache) {
            demand_eviction(req, 'X', &gc_flag, &d_flag);
        }

        t_ppa = c_table->t_ppa;

        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_M, NULL, NULL);
        params = (demand_params *)temp_req->params;

        __demand.li->read(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);
        MS(&req->latency_poll);
        dl_sync_wait(&params->dftl_mutex);
        MA(&req->latency_poll);

        free(params);
        free(temp_req);

        c_table->p_table = p_table;
        c_table->queue_ptr = lru_push(lru, (void *)c_table);

        num_caching++;
    }

    /* Invalidate the page */
    ppa = p_table[P_IDX].ppa;

    // Validity check by ppa
    if (ppa == -1) { // case of no data written
        bench_algo_end(req);
        return UINT32_MAX;
    }

    p_table[P_IDX].ppa = -1;
    demand_OOB[ppa].lpa = -1;
    BM_InvalidatePage(bm, ppa);

    if (c_table->state == CLEAN) {
        c_table->state = DIRTY;
        BM_InvalidatePage(bm, t_ppa);

#if C_CACHE
        if (!c_table->queue_ptr) {
            // migrate
            if (num_caching == num_max_cache) {
                demand_eviction(req, 'X', &gc_flag, &d_flag);
            }
            c_table->queue_ptr = lru_push(lru, (void *)c_table);
            num_caching++;
        }
#endif
    }

    bench_algo_end(req);
    return 0;
}


uint32_t demand_set(request *const req){
    request *temp_req;

    if (trig_data_r > 100000) {
        printf("\nWAF: %.2f\n", (float)(data_r+dirty_evict_on_write)/data_r);
        trig_data_r = 0;
    }

    while((temp_req = (request*)q_dequeue(dftl_q))){
        if(__demand_get(temp_req) == UINT32_MAX){
            temp_req->type = FS_NOTFOUND_T;
            temp_req->end_req(temp_req);
        }
    }
    __demand_set(req);
#ifdef W_BUFF
    if(write_buffer->size == max_write_buf){
        return 1;
    }
#endif
    return 0;
}

uint32_t demand_get(request *const req){
    request *temp_req;

    while((temp_req = (request*)q_dequeue(dftl_q))){
        if(__demand_get(temp_req) == UINT32_MAX){
            temp_req->type = FS_NOTFOUND_T;
            temp_req->end_req(temp_req);
        }
    }
    if(__demand_get(req) == UINT32_MAX){
        req->type = FS_NOTFOUND_T;
        req->end_req(req);
    }
    return 0;
}

uint32_t demand_remove(request *const req) {
    request *temp_req;
    while ((temp_req = (request *)q_dequeue(dftl_q))) {
        if (__demand_get(temp_req) == UINT32_MAX) {
            temp_req->type = FS_NOTFOUND_T;
            temp_req->end_req(temp_req);
        }
    }

    __demand_remove(req);
    req->end_req(req);
    return 0;
}

#if C_CACHE
uint32_t demand_eviction(request *const req, char req_t, bool *flag, bool *dflag) {
    int32_t   t_ppa;
    C_TABLE   *cache_ptr;
    value_set *p_table_vs;
    D_TABLE   *p_table;
    value_set *temp_value_set;
    algo_req  *temp_req;
    value_set *dummy_vs;

    /* Eviction */
    evict_count++;

    if (req_t == 'R') { // Eviction on read -> only clean eviction
        clean_eviction++;
        clean_evict_on_read++;

        cache_ptr = (C_TABLE *)lru_pop(c_lru);
        p_table   = cache_ptr->p_table;

        cache_ptr->clean_ptr = NULL;

        // clear only when the victim page isn't still on the dirty cache
        if (cache_ptr->queue_ptr == NULL) {
            //inf_free_valueset(p_table_vs, FS_MALLOC_R);
            //cache_ptr->p_table_vs = NULL;
            cache_ptr->p_table = NULL;
        }
        num_clean--;

    } else { // Eviction on write
        dirty_eviction++;
        dirty_evict_on_write++;

        cache_ptr = (C_TABLE *)lru_pop(lru);
        //p_table_vs = cache_ptr->p_table_vs;

        *dflag = true;

        /* Write translation page */
        t_ppa = tp_alloc(req_t, flag);
        //temp_value_set = inf_get_valueset((PTR)p_table, FS_MALLOC_W, PAGESIZE);
        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_W, dummy_vs, NULL);
#if EVICT_POLL
        params = (demand_params *)temp_req->params;
#endif
        __demand.li->write(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);
#if EVICT_POLL
        pthread_mutex_lock(&params->dftl_mutex);
        pthread_mutex_destory(&params->dftl_mutex);
        free(params);
        free(temp_req);
#endif
        demand_OOB[t_ppa].lpa = cache_ptr->idx;
        BM_ValidatePage(bm, t_ppa);

        cache_ptr->t_ppa = t_ppa;
        cache_ptr->state = CLEAN;

        cache_ptr->queue_ptr = NULL;

        // clear only when the victim page isn't on the clean cache
        if (cache_ptr->clean_ptr == NULL) {
            //inf_free_valueset(p_table_vs, FS_MALLOC_R);
            //cache_ptr->p_table_vs = NULL;
            cache_ptr->p_table = NULL;
        }
        num_caching--;
    }
    return 1;
}

#else // Eviction for normal mode
uint32_t demand_eviction(request *const req, char req_t, bool *flag, bool *dflag){
    int32_t   t_ppa;            // Translation page address
    C_TABLE   *cache_ptr;       // Cache mapping entry pointer
    algo_req  *temp_req;        // pseudo request pointer
    value_set *dummy_vs;
#if EVICT_POLL
    demand_params *params;
#endif

    //printf("num_caching : %d\n", num_caching);
    /* Eviction */
    evict_count++;

    cache_ptr = (C_TABLE*)lru_pop(lru); // call pop to get least used cache
    //p_table   = cache_ptr->p_table;
    t_ppa     = cache_ptr->t_ppa;

    if(cache_ptr->state == DIRTY){ // When t_page on cache has changed
        dirty_eviction++;
        if (req_t == 'W') {
            dirty_evict_on_write++;
        } else {
            dirty_evict_on_read++;
        }

        *dflag = true;

        /* Write translation page */
        t_ppa = tp_alloc(req_t, flag);

        demand_OOB[t_ppa].lpa = cache_ptr->idx;

        cache_ptr->queue_ptr = NULL;
        cache_ptr->p_table   = NULL;
        cache_ptr->t_ppa = t_ppa;
        cache_ptr->state = CLEAN;
        BM_ValidatePage(bm, t_ppa);

        num_caching--;

        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_W, dummy_vs, req);
#if EVICT_POLL
        params = (demand_params*)temp_req->params;
#endif
        __demand.li->write(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);
#if EVICT_POLL
        pthread_mutex_lock(&params->dftl_mutex);
        pthread_mutex_destroy(&params->dftl_mutex);
        free(params);
        free(temp_req);
#endif
        return 0;

    } else {
        clean_eviction++;
        if (req_t == 'W') {
            clean_evict_on_write++;
        } else {
            clean_evict_on_read++;
        }
    }

    cache_ptr->queue_ptr = NULL;
    cache_ptr->p_table   = NULL;

    num_caching--;

    return 1;
}
#endif

void *demand_end_req(algo_req* input){
    demand_params *params = (demand_params*)input->params;
    value_set *temp_v = params->value;
    request *res = input->parents;

    switch(params->type){
        case DATA_R:
            data_r++; trig_data_r++;

            res->type_lower = input->type_lower;
            if(res){
                res->end_req(res);
            }
            break;
        case DATA_W:
            data_w++;

#if W_BUFF
            inf_free_valueset(temp_v, FS_MALLOC_W);
#endif
            if(res){
                res->end_req(res);
            }
            break;
        case MAPPING_R: // only used in async
            trans_r++;

            ((read_params*)res->params)->read = 1;
            if(!inf_assign_try(res)) {
                puts("not queued 1");
                q_enqueue((void*)res, dftl_q);
            }
            inf_free_valueset(temp_v, FS_MALLOC_R);

            break;
        case MAPPING_W:
            trans_w++;
            if(!inf_assign_try(res)) {
                puts("not queued 2");
                q_enqueue((void*)res, dftl_q);
            }

            inf_free_valueset(temp_v, FS_MALLOC_W);
#if EVICT_POLL
            pthread_mutex_unlock(&params->dftl_mutex);
#endif
            break;
        case MAPPING_M: // unlock mutex lock for read mapping data completely
            trans_r++;

            inf_free_valueset(temp_v, FS_MALLOC_R);
            break;
        case TGC_R:
            tgc_r++;

            trans_gc_poll++;
            break;
        case TGC_W:
            tgc_w++;

            inf_free_valueset(temp_v, FS_MALLOC_W);
#if GC_POLL
            trans_gc_poll++;
#endif
            break;
        case TGC_M:
            tgc_r++;

            dl_sync_arrive(&params->dftl_mutex);
#if GC_POLL
            trans_gc_poll++;
#endif
            return NULL;
            break;
        case DGC_R:
            dgc_r++;

            data_gc_poll++;
            break;
        case DGC_W:
            dgc_w++;

            inf_free_valueset(temp_v, FS_MALLOC_W);
#if GC_POLL
            data_gc_poll++;
#endif
            break;
    }

    free(params);
    free(input);
    return NULL;
}
