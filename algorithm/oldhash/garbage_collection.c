#include "dftl.h"

//#define LEAKCHECK

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
        d_sram[i].OOB_RAM.lpa = -1;
        d_sram[i].origin_ppa = -1;
    }

    /* read valid pages in block */
    for(int i = old_block; i < old_block + p_p_b; i++){
        if(BM_IsValidPage(bm, i)){ // read valid page
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
        CMT[d_sram[i].OOB_RAM.lpa].t_ppa = new_block + i;
        SRAM_unload(d_sram, new_block + i, i, 'T');
    }

#if GC_POLL
    while(trans_gc_poll != valid_page_num) {} // polling for reading all mapping data
#endif

//    free(temp_set);
    free(d_sram);

    /* Trim block */
    __demand.li->trim_block(old_block, false);

	printf(" - %d\n", valid_page_num);

    return new_block + valid_page_num;
}

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

    /* Load valid pages to SRAM */
    all = 0;
    dgc_count++;
    victim = BM_Heap_Get_Max(data_b);
    if(victim->Invalid == p_p_b){ // if all invalid block
        all = 1;
    }
    else if(victim->Invalid == 0){
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
        d_sram[i].OOB_RAM.lpa = -1;
        d_sram[i].origin_ppa = -1;
    }

    /* read valid pages in block */
    for(int i = old_block; i < old_block + p_p_b; i++){
        if(BM_IsValidPage(bm, i)){
            temp_set[valid_num] = SRAM_load(d_sram, i, valid_num, 'D');
            valid_num++;
        }
    }

    BM_InitializeBlock(bm, victim->PBA);

    while(data_gc_poll != valid_num) {
#ifdef LEAKCHECK
        sleep(1);
#endif
    }// polling for reading all data

#if GC_POLL
    data_gc_poll = 0;
#endif
    
    for(int i = 0; i < valid_num; i++){
#if MEMCPY_ON_GC
        memcpy(d_sram[i].DATA_RAM, temp_set[i]->value, PAGESIZE);
#endif
        inf_free_valueset(temp_set[i], FS_MALLOC_R);
    }

    /* Sort pages in SRAM */
    qsort(d_sram, p_p_b, sizeof(D_SRAM), lpa_compare); // Sort valid pages by lpa order

    /* Manage mapping data and write tpages */
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
            else{ // but CLEAN state couldn't have this case, so just change ppa
                p_table[P_IDX].ppa = new_block + real_valid;
                real_valid++;
                if(c_table->state == CLEAN){
                    c_table->state = DIRTY;
                    BM_InvalidatePage(bm, t_ppa);
#if C_CACHE
                    if (num_caching == num_max_cache) {
                        bool gc_flag, d_flag;
                        demand_eviction(NULL, 'D', &gc_flag, &d_flag);
                    }

                    c_table->queue_ptr = lru_push(lru, (void *)c_table);
                    num_caching++;
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
            if(tce != d_sram[i + 1].OOB_RAM.lpa/EPP && tce != INT32_MAX){
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

#if GC_POLL
    while(data_gc_poll != real_valid + twrite) {} // polling for reading all data
#endif

    //free(temp_table);
    free(temp_set);
    free(d_sram);

    /* Trim data block */
    __demand.li->trim_block(old_block, false);

	printf(" - %d\n", real_valid);

    return new_block + real_valid;
}

