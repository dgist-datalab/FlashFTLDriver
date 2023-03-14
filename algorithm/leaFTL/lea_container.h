#pragma once
#include <stdint.h>
#include <stdlib.h>
typedef struct temp_map{
    uint32_t size;
    int32_t *interval;
    uint32_t *lba;
    uint32_t *piece_ppa;
}temp_map;

static inline temp_map* temp_map_assign(uint32_t size){
    temp_map *res=(temp_map*)malloc(sizeof(temp_map));
    res->lba=(uint32_t*)malloc(sizeof(uint32_t) * size);
    res->interval=(int32_t*)malloc(sizeof(int32_t) * size);
    res->piece_ppa=(uint32_t*)malloc(sizeof(uint32_t) * size);
    res->size=size;
    return res;
}

static inline void temp_map_clear(temp_map *map){
    map->size=0;
}

static inline void temp_map_free(temp_map *t){
    free(t->lba);
    free(t->piece_ppa);
    free(t->interval);
    free(t);
}