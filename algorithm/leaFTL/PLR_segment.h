#pragma once
#include "./plr/plr.h"
#include "./lea_container.h"

typedef enum SEGMENT_TYPE{
    ACCURATE, APPROXIMATE
}SEGMENT_TYPE;

typedef union content{
    uint32_t interval;
    PLR *plr;
} content;

typedef struct segment{
    SEGMENT_TYPE type;
    uint32_t original_start;
    uint32_t start;
    uint32_t end;
    uint32_t start_piece_ppa;
    content body;
}segment;

segment *segment_make(temp_map *map, SEGMENT_TYPE type, uint32_t interval);
uint64_t segment_size(segment *seg);
uint32_t segment_get_addr(segment *seg, uint32_t lba);
/*new seg must be ACCTYPE*/
bool segment_removable(segment *old_seg, segment *new_seg);
bool segment_acc_include(segment *acc_seg, uint32_t lba);
void segment_free(segment* seg);
void segment_update(segment *seg, uint32_t start, uint32_t end);
void segment_print(segment *seg);
