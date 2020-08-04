#include "../../include/settings.h"
#include "../../include/container.h"

uint32_t amf_info_create(lower_info *li, blockmanager *bm);
void* amf_info_destroy(lower_info *li);

void* amf_info_write(uint32_t ppa, uint32_t size, value_set *value,bool async,algo_req * const req);
void* amf_info_read(uint32_t ppa, uint32_t size, value_set *value,bool async,algo_req * const req);
void* amf_info_device_badblock_checker(uint32_t ppa,uint32_t size,void *(*process)(uint64_t, uint8_t));
void* amf_info_trim_block(uint32_t ppa,bool async);
void* amf_info_trim_a_block(uint32_t ppa,bool async);
void* amf_info_refresh(struct lower_info*);
void amf_info_stop();
void amf_info_show_info();
uint32_t amf_info_lower_tag_num();
void amf_flying_req_wait();
