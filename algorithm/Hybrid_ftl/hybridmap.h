//
// Created by minkijo on 21. 7. 1..
//
#ifndef FLASHFTLDRIVER_BLOOMFTL_HYBRIDMAP_H
#define FLASHFTLDRIVER_BLOOMFTL_HYBRIDMAP_H

#include "../../include/container.h"
#include "../../interface/interface.h"


typedef struct lb{
    bool empty;
    uint32_t db_idx;
    uint32_t cnt;
    uint32_t lbmapping[_PPS]; //for page mapping

    __segment *plb; //physical area for log block
}lb_t;


typedef struct db{
    uint32_t lb_idx;
    uint32_t  pba;
}db_t;

typedef struct hybrid_map_body{
    db_t * datablock; //data block
    lb_t * logblock; //log block
    //uint32_t * dlmapping; //datablock - logblock mapping
}hm_body;


void hybrid_map_create();
uint32_t find_empty_lb();
uint32_t hybrid_map_assign(KEYT* lba, uint32_t max_idx);
uint32_t hybrid_map_pick(uint32_t lba);



#endif //FLASHFTLDRIVER_BLOOMFTL_HYBRIDMAP_H
