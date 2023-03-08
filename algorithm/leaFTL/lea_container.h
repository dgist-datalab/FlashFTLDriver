#pragma once
#include <stdint.h>
typedef struct temp_map{
    uint32_t size;
    uint32_t *lba;
    uint32_t *piece_ppa;
}temp_map;