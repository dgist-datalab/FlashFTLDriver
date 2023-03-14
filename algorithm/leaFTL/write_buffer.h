#pragma once
#include "../../include/settings.h"
#include "../../include/container.h"
#include "./lea_container.h"
#include "./page_manager.h"
#include <map>

typedef struct lea_write_buffer{
    uint32_t ps_ptr; //physical_sector_ptr;
    uint32_t max_ps_ptr;
    char **sector_buffer; //data_logger;
    std::map<uint32_t, uint32_t> L2P_map; 
    align_buffer page_buf;
}lea_write_buffer;


lea_write_buffer *lea_write_buffer_init(uint32_t max_write_buffer_byte);
bool lea_write_buffer_flush(lea_write_buffer *wb, page_manager *pm, temp_map *tmap, uint32_t page_size);
void lea_write_buffer_clear(lea_write_buffer *wb);
bool lea_write_buffer_isfull(lea_write_buffer *wb);
uint32_t lea_write_buffer_insert(lea_write_buffer *wb, uint32_t lba, char *data);
char * lea_write_buffer_get(lea_write_buffer *wb, uint32_t lba);
void lea_write_buffer_free(lea_write_buffer *wb);