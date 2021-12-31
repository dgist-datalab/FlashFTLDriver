#include "./merge_helper.h"

merge_helper *mh_init(){
    merge_helper *res=(merge_helper*)calloc(1, sizeof(merge_helper));
    res->spm_set=(summary_page_meta**)malloc(2 *sizeof(summary_page_meta*));
    res->allocated_num=2;
    return res;
}

uint32_t mh_insert(merge_helper *mh, summary_page_meta *spm){
    if(mh->insert_pointer==mh->allocated_num){
        mh->allocated_num*=2;
        mh->spm_set=(summary_page_meta**)realloc(mh->spm_set, mh->allocated_num*sizeof(summary_page_meta*));
    }
    mh->spm_set[mh->insert_pointer]=spm;
    mh->insert_pointer++;
    return true;
}

summary_page_meta *mh_get_logical_copy_target(merge_helper *mh){
    if(mh->read_pointer==mh->insert_pointer) return NULL;
    summary_page_meta *target=mh->spm_set[mh->read_pointer];
    return target;
}

int cmpfunc(const void *a, const void *b){
    return ((summary_page_meta*)a)->start_lba - ((summary_page_meta*)b)->start_lba;
}

void mh_sort(merge_helper *mh){
    qsort(mh->spm_set, mh->insert_pointer, sizeof(summary_page_meta*), cmpfunc);
}

void mh_move_forward(merge_helper *mh){
    mh->read_pointer++;
}

void mh_free(merge_helper *mh){
    free(mh->spm_set);
    free(mh);
}