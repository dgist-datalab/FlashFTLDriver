#include "dftl.h"

//#define LEAKCHECK

struct gc_bucket_node {
	PTR ptr;
	int32_t lpa;
	int32_t ppa;
};
struct gc_bucket {
	struct gc_bucket_node bucket[PAGESIZE/GRAINED_UNIT+1][2048];
	uint16_t idx[PAGESIZE/GRAINED_UNIT+1];
};

static int lpa_compare(const void *a, const void *b){
#ifdef DVALUE
    uint32_t num1 = (int32_t)((struct gc_bucket_node *)a)->lpa;
    uint32_t num2 = (int32_t)((struct gc_bucket_node *)b)->lpa;
#else
    uint32_t num1 = (uint32_t)(((D_SRAM*)a)->OOB_RAM.lpa);
    uint32_t num2 = (uint32_t)(((D_SRAM*)b)->OOB_RAM.lpa);
#endif
    if(num1 < num2){
        return -1;
    }
    else if(num1 == num2){
        return 0;
    }
    else{
        return 1;
    }
}

int32_t tpage_GC(){
    int32_t old_block;
    int32_t new_block;
    uint8_t all;
    volatile int valid_page_num;
    Block *victim;
    value_set **temp_set;
    D_SRAM *d_sram; // SRAM for contain block data temporarily

    /* Load valid pages to SRAM */
    all = 0;
    tgc_count++;
    victim = BM_Heap_Get_Max(trans_b);
    if(victim->Invalid == p_p_b){ // if all invalid block
        all = 1;
    }
    else if(victim->Invalid == 0){
        printf("\n!!!tp_full!!!\n");
        exit(2);
    }
    //exchange block
    victim->type = 0;
    old_block = victim->PBA * p_p_b;
    new_block = t_reserved->PBA * p_p_b;
    t_reserved->type = 1;
    t_reserved->hn_ptr = BM_Heap_Insert(trans_b, t_reserved);
    t_reserved = victim;
    if(all){ // if all page is invalid, then just trim and return
		puts("tpage_GC() - all");
        __demand.li->trim_block(old_block, false);
        BM_InitializeBlock(bm, victim->PBA);
        return new_block;
    }

	printf("tpage_GC()");

    valid_page_num = 0;
    trans_gc_poll = 0;
    d_sram = (D_SRAM*)malloc(sizeof(D_SRAM) * p_p_b); //필요한 만큼만 할당하는 걸로 변경
    temp_set = (value_set**)malloc(sizeof(value_set*) * p_p_b);

    for(int i = 0; i < p_p_b; i++){
        d_sram[i].DATA_RAM = NULL;
#ifdef DVALUE
        d_sram[i].OOB_RAM.lpa[0] = -1;
#else
        d_sram[i].OOB_RAM.lpa = -1;
#endif
        d_sram[i].origin_ppa = -1;
    }

    /* read valid pages in block */
    for(int i = old_block; i < old_block + p_p_b; i++){
#ifdef DVALUE
		int idx = i * GRAIN_PER_PAGE;
#else
		int idx = i;
#endif
        if(BM_IsValidPage(bm, idx)){ // read valid page
            temp_set[valid_page_num] = SRAM_load(d_sram, i, valid_page_num, 'T');
            valid_page_num++;
        }
    }

    BM_InitializeBlock(bm, victim->PBA);

    while(trans_gc_poll != valid_page_num) {
#ifdef LEAKCHECK
        sleep(1);
#endif
    } // polling for reading all mapping data

#if GC_POLL
    trans_gc_poll = 0;
#endif

    for(int i = 0; i < valid_page_num; i++){ // copy data to memory and free dma valueset
#if MEMCPY_ON_GC
        memcpy(d_sram[i].DATA_RAM, temp_set[i]->value, PAGESIZE);
#endif
        inf_free_valueset(temp_set[i], FS_MALLOC_R); //미리 value_set을 free시켜서 불필요한 value_set 낭비 줄임
    }

    for(int i = 0; i < valid_page_num; i++){ // write page into new block
#ifdef DVALUE
        CMT[d_sram[i].OOB_RAM.lpa[0]].t_ppa = new_block + i;
#else
        CMT[d_sram[i].OOB_RAM.lpa].t_ppa = new_block + i;
#endif
        SRAM_unload(d_sram, new_block + i, i, 'T');
    }

#if GC_POLL
    while(trans_gc_poll != valid_page_num) {} // polling for reading all mapping data
#endif

    free(temp_set);
    free(d_sram);

    /* Trim block */
    __demand.li->trim_block(old_block, false);

	printf(" - %d\n", valid_page_num);

    return new_block + valid_page_num;
}

static int dpage_valid_check() {
	int nr_total_invalid = 0;
	int nr_total_page = 0;

	int nr_data_block = data_b->max_size;
	h_node *heap_array = data_b->body;

	for (int i = 0; i < nr_data_block; i++) {
		if (heap_array[i].value) {
			Block *b = (Block *)heap_array[i].value;

			nr_total_invalid += b->Invalid;
			nr_total_page += _PPS;
		}
	}

	printf("data page status ( %d / %d )\n", nr_total_page - nr_total_invalid, nr_total_page);
	printf("Utilization: %.4f%%\n", (float)(nr_total_page - nr_total_invalid) / nr_total_page*100);

	return 0;
}

#ifdef DVALUE
static bool valid_check_grain2page(BM_T *bm, int32_t ppa) {
	for (int i = 0; i < GRAIN_PER_PAGE; i++) {
		if (BM_IsValidPage(bm, ppa*GRAIN_PER_PAGE+i)) {
			return true;
		}
	}
	return false;
}
#endif

struct gc_bucket_node *gcb_node_arr[_PPS*GRAIN_PER_PAGE];
int32_t dpage_GC(){
    uint8_t all;
    int32_t lpa;
    int32_t tce; // temp_cache_entry index
    int32_t t_ppa;
    int32_t old_block;
    int32_t new_block;
    volatile int32_t twrite;
    volatile int valid_num;
    volatile int real_valid;
    Block *victim;
    C_TABLE *c_table;
    //value_set *p_table_vs;
    D_TABLE *p_table;
    //D_TABLE* on_dma;
    D_TABLE *temp_table;
    D_SRAM *d_sram; // SRAM for contain block data temporarily
    algo_req *temp_req;
    demand_params *params;
    value_set *temp_value_set;
    value_set **temp_set;
    value_set *dummy_vs;

	dpage_valid_check();

    /* Load valid pages to SRAM */
    all = 0;
    dgc_count++;
    victim = BM_Heap_Get_Max(data_b);
    /*if(victim->Invalid == p_p_b){ // if all invalid block
        all = 1;
		puts(" * full eviction");
    }
    else*/ if(victim->Invalid == 0){
        printf("\n!!!dp_full!!!\n");
        exit(3);
    }
    //exchange block
    victim->Invalid = 0;
    victim->type = 0;
    old_block = victim->PBA * p_p_b;
    new_block = d_reserved->PBA * p_p_b;
    d_reserved->type = 2;
    d_reserved->hn_ptr = BM_Heap_Insert(data_b, d_reserved);
    d_reserved = victim;
    if(all){ // if all page is invalid, then just trim and return
		puts("dpage_GC - all");
        __demand.li->trim_block(old_block, false);
        return new_block;
    }
	printf("dpage_GC");

    valid_num = 0;
    real_valid = 0;
    data_gc_poll = 0;
    twrite = 0;
    tce = INT32_MAX; // Initial state
    temp_table = (D_TABLE *)malloc(PAGESIZE);
    d_sram = (D_SRAM*)malloc(sizeof(D_SRAM) * p_p_b);
    temp_set = (value_set**)malloc(sizeof(value_set*) * p_p_b);

    for(int i = 0; i < p_p_b; i++){
        d_sram[i].DATA_RAM = NULL;
#ifdef DVALUE
		for (int j = 0; j < GRAIN_PER_PAGE; j++) {
			d_sram[i].OOB_RAM.lpa[j] = -1;
		}
#else
		d_sram[i].OOB_RAM.lpa = -1;
#endif
        d_sram[i].origin_ppa = -1;
    }

    /* read valid pages in block */
    for(int i = old_block; i < old_block + p_p_b; i++){
#ifdef DVALUE
        if(valid_check_grain2page(bm, i)){
#else
        if(BM_IsValidPage(bm, i)){
#endif
            temp_set[valid_num] = SRAM_load(d_sram, i, valid_num, 'D');
            valid_num++;
        }
    }

    while(data_gc_poll != valid_num) {
#ifdef LEAKCHECK
        sleep(1);
#endif
    }// polling for reading all data

#if GC_POLL
    data_gc_poll = 0;
#endif

#ifdef DVALUE
	struct gc_bucket *gc_bucket = (struct gc_bucket *)malloc(sizeof(struct gc_bucket));
	int total_valid_grain = 0;
	for (int i = 0; i < valid_num; i++) {
		for (int j = 0; j < GRAIN_PER_PAGE; j++) {
			if (BM_IsValidPage(bm, d_sram[i].origin_ppa*GRAIN_PER_PAGE + j)) {
				int len = 1;
				while (j + len < GRAIN_PER_PAGE && d_sram[i].OOB_RAM.lpa[j+len] == -1) {
					len++;
				}

				struct gc_bucket_node *gcb_node = &gc_bucket->bucket[len][gc_bucket->idx[len]];
				gcb_node->ptr = temp_set[i]->value + j*GRAINED_UNIT;
				gcb_node->lpa = d_sram[i].OOB_RAM.lpa[j];

				gc_bucket->idx[len]++;

				gcb_node_arr[total_valid_grain] = gcb_node;
				total_valid_grain++;
			}
		}
	}

	int ordering_done = 0, copied_pages = 0;
	while (ordering_done < total_valid_grain) {
		value_set *new_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		PTR page = new_vs->value;
		int remain = PAGESIZE;
		int32_t ppa = new_block + copied_pages;
		int offset = 0;

		while (remain > 0) {
			int target_length = remain / GRAINED_UNIT;
			while(gc_bucket->idx[target_length]==0 && target_length!=0) --target_length;
			if (target_length==0) {
				break;
			}

			struct gc_bucket_node *gcb_node = &gc_bucket->bucket[target_length][gc_bucket->idx[target_length]-1];
			gc_bucket->idx[target_length]--;

			gcb_node->ppa = ppa * GRAIN_PER_PAGE + offset;
			memcpy(&page[offset*GRAINED_UNIT], gcb_node->ptr, target_length*GRAINED_UNIT);

			offset += target_length;
			remain -= target_length * GRAINED_UNIT;

			ordering_done++;
			BM_ValidatePage(bm, gcb_node->ppa);
		}
		algo_req *my_req = assign_pseudo_req(DGC_W, new_vs, NULL);
		__demand.li->write(ppa, PAGESIZE, new_vs, ASYNC, my_req);

		copied_pages++;
	}


    BM_InitializeBlock(bm, victim->PBA);
	for (int i = 0; i < valid_num; i++) {
        inf_free_valueset(temp_set[i], FS_MALLOC_R);
	}
#else
    for(int i = 0; i < valid_num; i++){
#if MEMCPY_ON_GC
        memcpy(d_sram[i].DATA_RAM, temp_set[i]->value, PAGESIZE);
#endif
        inf_free_valueset(temp_set[i], FS_MALLOC_R);
    }
#endif 

    /* Sort pages in SRAM */
#ifdef DVALUE
	qsort(gcb_node_arr, total_valid_grain, sizeof(struct gc_bucket_node *), lpa_compare);
#else
    qsort(d_sram, p_p_b, sizeof(D_SRAM), lpa_compare); // Sort valid pages by lpa order
#endif

    /* Manage mapping data and write tpages */
#ifdef DVALUE
	for (int i = 0; i < total_valid_grain; i++) {
		struct gc_bucket_node *gcb_node = gcb_node_arr[i];
		lpa = gcb_node->lpa;

        c_table = &CMT[D_IDX];
        t_ppa = c_table->t_ppa;
        p_table = c_table->p_table;

        if(p_table){ // cache hit
            /*if(c_table->state == DIRTY && p_table[P_IDX].ppa != d_sram[i].origin_ppa){
                d_sram[i].origin_ppa = -1; // if not same as origin, it mean this is actually invalid data
                continue;
            }
            else*/{ // but flag 0 couldn't have this case, so just change ppa
                p_table[P_IDX].ppa = gcb_node->ppa;
                real_valid++;
                if(c_table->state == CLEAN){
                    c_table->state = DIRTY;
                    BM_InvalidatePage(bm, t_ppa * GRAIN_PER_PAGE);
                }
            }
            continue;
        }

        // cache miss
        if(tce == INT32_MAX){ // read t_page into temp_table
            tce = D_IDX;
            temp_value_set = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
            temp_req = assign_pseudo_req(TGC_M, temp_value_set, NULL);
            params = (demand_params*)temp_req->params;
            __demand.li->read(t_ppa, PAGESIZE, temp_value_set, ASYNC, temp_req);
            dl_sync_wait(&params->dftl_mutex);
            //memcpy(temp_table, temp_value_set->value, PAGESIZE);
            temp_table = mem_arr[tce].mem_p;
            free(params);
            free(temp_req);
            inf_free_valueset(temp_value_set, FS_MALLOC_R);
        }
        temp_table[P_IDX].ppa = gcb_node->ppa;
        real_valid++;
        if(i != valid_num -1){ // check for flush changed t_page.
			lpa = d_sram[i+1].OOB_RAM.lpa[0];
            if(tce != lpa/EPP && tce != INT32_MAX){
                tce = INT32_MAX;
            }
        }
        else{
            tce = INT32_MAX;
        }
        if(tce == INT32_MAX){ // flush temp table into device
            BM_InvalidatePage(bm, t_ppa * GRAIN_PER_PAGE);
            twrite++;
            t_ppa = tp_alloc('D', NULL);
            //temp_value_set = inf_get_valueset((PTR)temp_table, FS_MALLOC_W, PAGESIZE); // Make valueset to WRITEMODE
            dummy_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
            __demand.li->write(t_ppa, PAGESIZE, dummy_vs, ASYNC, assign_pseudo_req(TGC_W, dummy_vs, NULL)); // Unload page to ppa
            demand_OOB[t_ppa].lpa[0] = c_table->idx;
            BM_ValidatePage(bm, t_ppa * GRAIN_PER_PAGE);
            c_table->t_ppa = t_ppa; // Update CMT t_ppa
        }

	}
#else
    for(int i = 0; i < valid_num; i++){
        lpa = d_sram[i].OOB_RAM.lpa; // Get lpa of a page

        c_table = &CMT[D_IDX];
        t_ppa = c_table->t_ppa;
        p_table = c_table->p_table;

        if(p_table){ // cache hit
            if(c_table->state == DIRTY && p_table[P_IDX].ppa != d_sram[i].origin_ppa){
                d_sram[i].origin_ppa = -1; // if not same as origin, it mean this is actually invalid data
                continue;
            }
            else{ // but flag 0 couldn't have this case, so just change ppa
                p_table[P_IDX].ppa = new_block + real_valid;
                real_valid++;
                if(c_table->state == CLEAN){
                    c_table->state = DIRTY;
                    BM_InvalidatePage(bm, t_ppa);
#if C_CACHE
                    /*if (num_caching == num_max_cache) {
                        bool gc_flag, d_flag;
                        demand_eviction(NULL, 'D', &gc_flag, &d_flag, NULL);
                    }

                    c_table->queue_ptr = lru_push(lru, (void *)c_table);
                    num_caching++; */
#endif
                }
            }
            continue;
        }

        // cache miss
        if(tce == INT32_MAX){ // read t_page into temp_table
            tce = D_IDX;
            temp_value_set = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
            temp_req = assign_pseudo_req(TGC_M, temp_value_set, NULL);
            params = (demand_params*)temp_req->params;
            __demand.li->read(t_ppa, PAGESIZE, temp_value_set, ASYNC, temp_req);
            dl_sync_wait(&params->dftl_mutex);
            //memcpy(temp_table, temp_value_set->value, PAGESIZE);
            temp_table = mem_arr[tce].mem_p;
            free(params);
            free(temp_req);
            inf_free_valueset(temp_value_set, FS_MALLOC_R);
        }
        temp_table[P_IDX].ppa = new_block + real_valid;
        real_valid++;
        if(i != valid_num -1){ // check for flush changed t_page.
			lpa = d_sram[i+1].OOB_RAM.lpa;
            if(tce != lpa/EPP && tce != INT32_MAX){
                tce = INT32_MAX;
            }
        }
        else{
            tce = INT32_MAX;
        }
        if(tce == INT32_MAX){ // flush temp table into device
            BM_InvalidatePage(bm, t_ppa);
            twrite++;
            t_ppa = tp_alloc('D', NULL);
            //temp_value_set = inf_get_valueset((PTR)temp_table, FS_MALLOC_W, PAGESIZE); // Make valueset to WRITEMODE
            dummy_vs = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
            __demand.li->write(t_ppa, PAGESIZE, dummy_vs, ASYNC, assign_pseudo_req(TGC_W, dummy_vs, NULL)); // Unload page to ppa
            demand_OOB[t_ppa].lpa = c_table->idx;
            BM_ValidatePage(bm, t_ppa);
            c_table->t_ppa = t_ppa; // Update CMT t_ppa
        }
    }


    /* Write dpages */
    real_valid = 0;
    for(int i = 0; i < valid_num; i++){
        if(d_sram[i].origin_ppa != -1){
            SRAM_unload(d_sram, new_block + real_valid++, i, 'D');
        }
        else{
            free(d_sram[i].DATA_RAM); // free without SRAM_unload, because this is not valid data
        }
    }
#endif

#if GC_POLL
#ifdef DVALUE
    while(data_gc_poll != copied_pages + twrite) {} // polling for reading all data
#else
    while(data_gc_poll != real_valid + twrite) {} // polling for reading all data
#endif
#endif

    //free(temp_table);
    free(temp_set);
    free(d_sram);

#ifdef DVALUE
	free(gc_bucket);
#endif

    /* Trim data block */
    __demand.li->trim_block(old_block, false);

	printf(" - %d\n", real_valid);
    return new_block + real_valid;
}

