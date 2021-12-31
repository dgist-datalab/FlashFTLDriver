#ifndef MERGE_HELPER_H
#define MERGE_HELPER_H

#include "./sorted_table.h"
#include "./summary_page.h"
#include <stdlib.h>

typedef struct merge_helper{
    summary_page_meta **spm_set;
    uint32_t allocated_num;
    uint32_t insert_pointer;
    uint32_t read_pointer;
}merge_helper;

merge_helper *mh_init();
uint32_t mh_insert(merge_helper *mh, summary_page_meta *spm);
void mh_sort(merge_helper *mh);
summary_page_meta *mh_get_logical_copy_target(merge_helper *mh);
void mh_move_forward(merge_helper *mh);
void mh_free(merge_helper *mh);

#endif