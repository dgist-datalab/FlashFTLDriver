#include "dftl.h"
#include "../../bench/bench.h"

algorithm __demand = {
    .create  = demand_create,
    .destroy = demand_destroy,
    .read    = demand_get,
    .write   = demand_set,
    .remove  = demand_remove
};

/*
   data에 관한 write buffer를 생성
   128개의 channel이라서 128개를 한번에 처리가능
   1024개씩 한번에 쓰도록.(dynamic)->변수처리
   ppa는 1씩 증가해서 보내도됨. ---->>>>> bdbm_drv 에서는 없어도 된다!!!!
   */

LRU *lru; // for lru cache
queue *dftl_q; // for async get
b_queue *free_b; // block allocate
Heap *data_b; // data block heap
Heap *trans_b; // trans block heap
#if W_BUFF
skiplist *mem_buf;
snode *dummy_snode;
#endif
queue *wait_q;
queue *write_q;
queue *flying_q;

C_TABLE *CMT; // Cached Mapping Table
D_OOB *demand_OOB; // Page OOB
mem_table* mem_arr;
request **waiting_arr;

BM_T *bm;
Block *t_reserved; // pointer of reserved block for translation gc
Block *d_reserved; // pointer of reserved block for data gc

int32_t num_caching; // Number of translation page on cache
volatile int32_t trans_gc_poll;
volatile int32_t data_gc_poll;

int32_t num_page;
int32_t num_block;
int32_t p_p_b;
int32_t num_tpage;
int32_t num_tblock;
int32_t num_dpage;
int32_t num_dblock;
int32_t max_cache_entry;
int32_t num_max_cache;
int32_t real_max_cache;
uint32_t max_sl;

volatile int32_t updated;
int32_t num_flying;
int32_t num_wflying;
int32_t waiting;

int32_t tgc_count;
int32_t dgc_count;
int32_t tgc_w_dgc_count;
int32_t read_tgc_count;
int32_t evict_count;
#if W_BUFF
int32_t buf_hit;
#endif

#if C_CACHE
LRU *c_lru;
int32_t num_clean; // Number of clean translation page on cache
int32_t max_clean_cache;
#endif

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
int32_t demand_req_num;

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
	printf(" | Write buffer size:      %d\n", max_sl);
	printf(" |\n");
	printf(" | ! Assume no Shadow buffer\n");
	//printf(" | ! PPAs are prefetched on write flush stage\n");
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
    //num_max_cache = max_cache_entry; // Full cache
    //num_max_cache = max_cache_entry / 4; // 25%
	//num_max_cache = max_cache_entry / 8; // 12.5%
    num_max_cache = max_cache_entry / 10; // 10%
    //num_max_cache = max_cache_entry / 20; // 5%
    //num_max_cache = 1; // 1 cache

    real_max_cache = num_max_cache;
    num_caching = 0;
#if C_CACHE
    max_clean_cache = num_max_cache / 2; // 50 : 50
    num_max_cache -= max_clean_cache;

    num_clean = 0;
#endif
    //max_sl = num_max_cache;
    max_sl = 1024;
    //max_sl = 512;

    /* Print information */
	print_algo_log();


    /* Map lower info */
    algo->li = li;


    /* Table allocation & Init */
    CMT = (C_TABLE*)malloc(sizeof(C_TABLE) * max_cache_entry);
    mem_arr = (mem_table *)malloc(sizeof(mem_table) * max_cache_entry);
    demand_OOB = (D_OOB*)malloc(sizeof(D_OOB) * num_page);
	waiting_arr = (request **)malloc(sizeof(request *) * max_sl);

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
        CMT[i].flying_arr = (request **)malloc(sizeof(request *) * max_sl);
        CMT[i].num_waiting = 0;

        CMT[i].wflying = false;
		CMT[i].flying_snodes = (snode **)malloc(sizeof(snode *) *max_sl);
        CMT[i].num_snode = 0;
    }

    memset(demand_OOB, -1, num_page * sizeof(D_OOB));

    // Create mem-table for CMTs
    for (int i = 0; i < max_cache_entry; i++) {
        mem_arr[i].mem_p = (int32_t *)malloc(PAGESIZE);
        memset(mem_arr[i].mem_p, -1, PAGESIZE);
    }


    /* Module Init */
    bm = BM_Init(num_block, p_p_b, 2, 1);
    t_reserved = &bm->barray[num_block - 2];
    d_reserved = &bm->barray[num_block - 1];

#if W_BUFF
    mem_buf = skiplist_init();
	dummy_snode = (snode *)malloc(sizeof(snode));
	dummy_snode->bypass = true;
#endif

    lru_init(&lru);
#if C_CACHE
    lru_init(&c_lru);
#endif

    q_init(&dftl_q, 1024);
    q_init(&wait_q, max_sl);
	q_init(&write_q, max_sl);
	q_init(&flying_q, max_sl);
    BM_Queue_Init(&free_b);
    for(int i = 0; i < num_block - 2; i++){
        BM_Enqueue(free_b, &bm->barray[i]);
    }
    data_b = BM_Heap_Init(num_dblock);
    trans_b = BM_Heap_Init(num_tblock);
    bm->harray[0] = data_b;
    bm->harray[1] = trans_b;
    bm->qarray[0] = free_b;
    return 0;
}

void demand_destroy(lower_info *li, algorithm *algo){
    /* Print information */
    printf("# of gc: %d\n", tgc_count + dgc_count);
    printf("# of translation page gc: %d\n", tgc_count);
    printf("# of data page gc: %d\n", dgc_count);
    printf("# of translation page gc w/ data page gc: %d\n", tgc_w_dgc_count);
    printf("# of translation page gc w/ read op: %d\n", read_tgc_count);
    printf("# of evict: %d\n", evict_count);
#if W_BUFF
    printf("# of buf hit: %d\n", buf_hit);
    skiplist_free(mem_buf);
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

	printf("%% of miss : %.2f\n",((float)(clean_eviction+dirty_eviction))/demand_req_num);
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
#if C_CACHE
	printf("num_clean:   %d\n", num_clean);
#endif
    printf("num_flying: %d\n\n", num_flying);

    /* Clear modules */
    q_free(dftl_q);
    q_free(wait_q);
	q_free(write_q);
	q_free(flying_q);
    BM_Free(bm);
    for (int i = 0; i < max_cache_entry; i++) {
        free(mem_arr[i].mem_p);
    }

    lru_free(lru);
#if C_CACHE
    lru_free(c_lru);
#endif

    /* Clear tables */
    free(mem_arr);
    free(demand_OOB);
    free(CMT);
	free(waiting_arr);
}

static uint32_t demand_cache_update(request *const req, char req_t) {
	int lpa = req->key;
	C_TABLE *c_table = &CMT[D_IDX];
    int32_t t_ppa = c_table->t_ppa;

#if C_CACHE
	bool gc_flag = false, d_flag = false;
#endif

    if (req_t == 'R') {
#if C_CACHE
        if (c_table->clean_ptr) {
			clean_hit_on_read++;
            lru_update(c_lru, c_table->clean_ptr);
        }
        if (c_table->queue_ptr) {
			dirty_hit_on_read++;
            lru_update(lru, c_table->queue_ptr);
        }
#else
        lru_update(lru, c_table->queue_ptr);

		if (c_table->state == DIRTY) dirty_hit_on_read++;
		else clean_hit_on_read;
#endif
    } else { // req_t == 'W'
        if (c_table->state == CLEAN) {
            c_table->state = DIRTY;
            BM_InvalidatePage(bm, t_ppa);
        }
        lru_update(lru, c_table->queue_ptr);
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

#if C_CACHE
	if (num_flying == max_clean_cache)
#else
    if (num_flying == num_max_cache) // This case occurs only if (QDEPTH > num_max_cache)
#endif
	{
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
        if (num_clean + num_flying == max_clean_cache) {
            req->type_ftl += 1;
            demand_eviction(req, 'R', &gc_flag, &d_flag, NULL); // Never mapping write
        }
#else
        if (num_caching + num_flying == num_max_cache) {
            req->type_ftl += 1;
            if (demand_eviction(req, 'R', &gc_flag, &d_flag, NULL) == 0) {
                c_table->flying = true;
                num_flying++;
                if(d_flag) req->type_ftl += 1;
                if(gc_flag) req->type_ftl += 2;
                bench_algo_end(req);
                return 1;
            }
        }
#endif
    } else { // req_t == 'W'
        if (num_caching + num_flying == num_max_cache) {
            if (demand_eviction(req, 'W', &gc_flag, &d_flag, NULL) == 0) {
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

        puts("wtf");

        return 1;
    }

    c_table->p_table = mem_arr[D_IDX].mem_p;

#if C_CACHE
	num_clean++;
#else 
    num_caching++;
#endif

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


uint32_t __demand_get(request *const req){
    int32_t lpa; // Logical data page address
    int32_t ppa; // Physical data page address
    int32_t t_ppa; // Translation page address
    C_TABLE* c_table; // Cache mapping entry pointer
    int32_t * p_table; // pointer of p_table on cme
#if W_BUFF
    snode *temp;
#endif

    bench_algo_start(req);
    lpa = req->key;
    if(lpa > RANGE + 1){ // range check
        printf("range error\n");
        exit(3);
    }

#if W_BUFF
    /* Check skiplist first */
    if((temp = skiplist_find(mem_buf, lpa))){
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
			ppa = p_table[P_IDX];
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
	} else {
		if (((read_params *)req->params)->read == 0) {
			if (demand_write_flying(req, 'R') == 1) {
				return 1;
			}
		} else {
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
	ppa = p_table[P_IDX];
	if (ppa == -1) {
		bench_algo_end(req);
		return UINT32_MAX;
	}
	bench_algo_end(req);
	// Get data in ppa
	__demand.li->read(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_R, NULL, req));

	return 1;
}

uint32_t __demand_set(request *const req){
    /* !!! you need to print error message and exit program, when you set more valid
       data than number of data page !!! */
    int32_t lpa; // Logical data page address
    int32_t ppa; // Physical data page address
    int32_t t_ppa; // Translation page address
    C_TABLE *c_table; // Cache mapping entry pointer
    int32_t *p_table; // pointer of p_table on cme
    algo_req *my_req; // pseudo request pointer
    bool gc_flag;
    bool d_flag;
    algo_req *temp_req;
    value_set *dummy_vs;

#if W_BUFF
    snode *temp;
    sk_iter *iter;
#endif

    bench_algo_start(req);
    gc_flag = false;
    d_flag = false;
    lpa = req->key;
    if(lpa > RANGE + 1){ // range check
        printf("range error\n");
        exit(3);
    }

    if (mem_buf->size == max_sl) {
        /* Push all the data to lower */
        iter = skiplist_get_iterator(mem_buf);
        for (size_t i = 0;i < max_sl; i++) {
            temp = skiplist_get_next(iter);

            /* Actual part of data push */
            ppa = dp_alloc();
            my_req = assign_pseudo_req(DATA_W, temp->value, NULL);
            __demand.li->write(ppa, PAGESIZE, temp->value, ASYNC, my_req);

            // Save ppa to snode (-> to update mapping info later)
            temp->ppa = ppa;
            temp->value = NULL; // this memory area will be freed in end_req

			q_enqueue((void *)temp, write_q);
        }

		/* Update mapping information */
		while (updated != max_sl) {
			temp = (snode *)q_dequeue(flying_q);
			if (temp != NULL) {
				lpa = temp->key;
				c_table = &CMT[D_IDX];
				p_table = c_table->p_table;
				t_ppa = c_table->t_ppa;

				if (temp->write_flying) {
					temp->write_flying = false;
					if (t_ppa != -1) {
						dummy_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
						temp_req = assign_pseudo_req(MAPPING_R, dummy_vs, NULL);
						((demand_params *)temp_req->params)->sn = temp;
                        temp->t_ppa = t_ppa;
						__demand.li->read(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);

						BM_InvalidatePage(bm, t_ppa);

						continue;
					}
				} else { // read flying
					// GC can occur while flying (t_ppa can be changed)
					if (temp->t_ppa != t_ppa) {
						temp->t_ppa = t_ppa;

						dummy_vs = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
						temp_req = assign_pseudo_req(MAPPING_R, dummy_vs, NULL);
						((demand_params *)temp_req->params)->sn = temp;

						__demand.li->read(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);

						continue;
					}
				}

				/* Case of initial state (t_ppa == -1) */
				c_table->p_table   = mem_arr[D_IDX].mem_p;
				c_table->queue_ptr = lru_push(lru, (void*)c_table);
				c_table->state     = DIRTY;
				num_caching++;

				// Register reserved requests
				for (int i = 0; i < c_table->num_snode; i++) {
					q_enqueue((void *)c_table->flying_snodes[i], wait_q);
					c_table->flying_snodes[i] = NULL;
				}
				c_table->num_snode = 0;
				c_table->wflying = false;
				num_wflying--;

				p_table = c_table->p_table;
				if (p_table[P_IDX] != -1) {
					BM_InvalidatePage(bm, p_table[P_IDX]);
				}

				// Update page table & OOB
				p_table[P_IDX] = temp->ppa;
				BM_ValidatePage(bm, temp->ppa);
				demand_OOB[temp->ppa].lpa = lpa;

				updated++;

				continue;
			}

			/* -------------------------------------------------------------- */

			if (num_wflying == num_max_cache) continue;

			temp = (snode *)q_dequeue(wait_q);
            if (temp == NULL) temp = (snode *)q_dequeue(write_q);
			if (temp == NULL) continue;

			lpa = temp->key;
			c_table = &CMT[D_IDX];
			p_table = c_table->p_table;
			t_ppa   = c_table->t_ppa;

			if (p_table) { // Cache hit
				cache_hit_on_write++;
#if C_CACHE
				if (c_table->state == CLEAN) {
					clean_hit_on_write++;

					c_table->state = DIRTY;
					BM_InvalidatePage(bm, t_ppa);

					// this page is dirty after hit, but still lies on clean lru
					lru_update(c_lru, c_table->clean_ptr);

					// migrate(copy) the lru element
					if (num_caching + num_wflying == num_max_cache) {
						demand_eviction(req, 'W', &gc_flag, &d_flag, NULL);
					}
					c_table->queue_ptr = lru_push(lru, (void *)c_table);
					num_caching++;

				} else { // Dirty hit
					dirty_hit_on_write++;

					if (c_table->clean_ptr) {
						lru_update(c_lru, c_table->clean_ptr);
					}
					lru_update(lru, c_table->queue_ptr);
				}
#else
				if (c_table->state == CLEAN) {
					clean_hit_on_write++;

					c_table->state = DIRTY;
					BM_InvalidatePage(bm, t_ppa);
				} else {
					dirty_hit_on_write;
				}
				lru_update(lru, c_table->queue_ptr);
#endif
				if (p_table[P_IDX] != -1) {
					BM_InvalidatePage(bm, p_table[P_IDX]);
				}

				// Update page table & OOB
				p_table[P_IDX] = temp->ppa;
				BM_ValidatePage(bm, temp->ppa);
				demand_OOB[temp->ppa].lpa = lpa;

				updated++;

			} else { // Cache miss
				if (c_table->wflying) {
					c_table->flying_snodes[c_table->num_snode++] = temp;
					continue;
				}
				cache_miss_on_write++;
				if (num_caching + num_wflying == num_max_cache) {
					demand_eviction(req, 'W', &gc_flag, &d_flag, temp);
					c_table->wflying = true;
					num_wflying++;
					continue;
				}

				c_table->p_table = mem_arr[D_IDX].mem_p;
				c_table->queue_ptr = lru_push(lru, (void *)c_table);
				c_table->state = DIRTY;
				num_caching++;

				p_table = c_table->p_table;
				p_table[P_IDX] = temp->ppa;
				demand_OOB[temp->ppa].lpa = lpa;

				updated++;
			}
		}

		// Clear the skiplist
		free(iter);
		skiplist_free(mem_buf);
		mem_buf = skiplist_init();

		updated = 0;

		// Wait until all flying requests(set) are finished
		__demand.li->lower_flying_req_wait();
    }

    /* Insert data to skiplist (default) */
    lpa = req->key;
    temp = skiplist_insert(mem_buf, lpa, req->value, true);
    req->value = NULL; // moved to value field of snode
    bench_algo_end(req);
    req->end_req(req);

    return 1;
}

uint32_t __demand_remove(request *const req) {
    int32_t lpa;
    int32_t ppa;
    int32_t t_ppa;
    C_TABLE *c_table;
    //value_set *p_table_vs;
    int32_t *p_table;
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
        printf("range error\n");
        exit(3);
    }

    c_table = &CMT[D_IDX];
    p_table = c_table->p_table;
    t_ppa   = c_table->t_ppa;

#if W_BUFF
    if (skiplist_delete(mem_buf, lpa) == 0) { // Deleted on skiplist
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
            demand_eviction(req, 'X', &gc_flag, &d_flag, NULL);
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
    //p_table = (int32_t *)p_table_vs->value;
    ppa = p_table[P_IDX];

    // Validity check by ppa
    if (ppa == -1) { // case of no data written
        bench_algo_end(req);
        return UINT32_MAX;
    }

    p_table[P_IDX] = -1;
    demand_OOB[ppa].lpa = -1;
    BM_InvalidatePage(bm, ppa);

    if (c_table->state == CLEAN) {
        c_table->state = DIRTY;
        BM_InvalidatePage(bm, t_ppa);

#if C_CACHE
        if (!c_table->queue_ptr) {
            // migrate
            if (num_caching == num_max_cache) {
                demand_eviction(req, 'X', &gc_flag, &d_flag, NULL);
            }
            c_table->queue_ptr = lru_push(lru, (void *)c_table);
            num_caching++;
        }
#endif
    }

    bench_algo_end(req);
    return 0;
}

uint32_t demand_get(request *const req){
    request *temp_req;
	if(req->params==NULL){
		demand_req_num++;
	}

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

uint32_t demand_set(request *const req){
    request *temp_req;
	if(req->params==NULL){
		demand_req_num++;
	}
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
    if(mem_buf->size == max_sl){
        return 1;
    }
#endif
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
uint32_t demand_eviction(request *const req, char req_t, bool *flag, bool *dflag, snode *sn) {
    int32_t   t_ppa;
    C_TABLE   *cache_ptr;
    int32_t   *p_table;
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
            cache_ptr->p_table = NULL;
        }
        num_clean--;

    } else { // Eviction on write
        dirty_eviction++;
        dirty_evict_on_write++;

        cache_ptr = (C_TABLE *)lru_pop(lru);

        *dflag = true;

        /* Write translation page */
        t_ppa = tp_alloc(req_t, flag);
        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_W, dummy_vs, NULL);
		if (!sn) sn = dummy_snode;
		sn->write_flying = true;
		((demand_params *)temp_req->params)->sn = sn;
        __demand.li->write(t_ppa, PAGESIZE, dummy_vs, ASYNC, temp_req);
        demand_OOB[t_ppa].lpa = cache_ptr->idx;
        BM_ValidatePage(bm, t_ppa);

        cache_ptr->t_ppa = t_ppa;
        cache_ptr->state = CLEAN;

        cache_ptr->queue_ptr = NULL;

        // clear only when the victim page isn't on the clean cache
        if (cache_ptr->clean_ptr == NULL) {
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
    int32_t   *p_table;         // physical page table on value_set
    //value_set *temp_value_set;  // valueset for write mapping table
    algo_req  *temp_req;        // pseudo request pointer
    value_set *dummy_vs;
#if EVICT_POLL
    demand_params *params;
#endif

    /* Eviction */
    evict_count++;

    cache_ptr = (C_TABLE*)lru_pop(lru); // call pop to get least used cache
    p_table   = cache_ptr->p_table;
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
        //temp_value_set = inf_get_valueset((PTR)p_table, FS_MALLOC_W, PAGESIZE);
        dummy_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
        temp_req = assign_pseudo_req(MAPPING_W, dummy_vs, NULL);
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
        demand_OOB[t_ppa].lpa = cache_ptr->idx;
        BM_ValidatePage(bm, t_ppa);
        cache_ptr->t_ppa = t_ppa;
        cache_ptr->state = CLEAN;
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
	snode *temp = params->sn;
    int32_t lpa;

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

			if (temp) {
				q_enqueue((void *)temp, flying_q);
			} else {
           		((read_params*)res->params)->read = 1;
				if(!inf_assign_try(res)){
					puts("not queued 1");
					while (!q_enqueue((void*)res, dftl_q)) {}
				}
			}

			inf_free_valueset(temp_v, FS_MALLOC_R);
            break;
        case MAPPING_W:
            trans_w++;
			if (temp) {
				if (!temp->bypass) q_enqueue((void *)temp, flying_q);
			} else {
				if (!inf_assign_try(res)) {
					puts("not queued 2");
					q_enqueue((void *)res, dftl_q);
				}
			}

            inf_free_valueset(temp_v, FS_MALLOC_W);
#if EVICT_POLL
            pthread_mutex_unlock(&params->dftl_mutex);
            return NULL;
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
