#pragma once
#include "../../include/settings.h"
#include <stdlib.h>
#include <stdint.h>

inline void data_copy(void *src, void *des, uint32_t size){
#ifdef NO_MEMCPY_DATA
    //do nothing
#else
    memcpy(src, des, size);
#endif
}

inline void data_move(void *src, void *des, uint32_t size){
#ifdef NO_MEMCPY_DATA
    //do nothing
#else
    memmove(src, des, size);
#endif
}

inline void data_set(void *des, uint8_t value, uint32_t size){
#ifdef NO_MEMCPY_DATA
    //do nothing
#else
    memset(des, value, size);
#endif
}