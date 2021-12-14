#ifndef AMF_INFO_H
#define AMF_INFO_H
#include "../../include/settings.h"
#include "../../include/container.h"
#include "./libamfdriver/AmfManager.h"

enum{
	LOWER_WRITE, LOWER_READ, LOWER_TRIM,
};

//#define TESTING
#define LOWER_MEM_DEV
#define REAL_PAGE_SIZE 8192
#define R2PGAP (PAGESIZE/REAL_PAGE_SIZE)
#define RPPB 256
#define AMF_PUNIT 128
#define REAL_PUNIT(ppa) (ppa&(AMF_PUNIT-1))

uint32_t amf_info_create(lower_info *li, blockmanager *bm);
void* amf_info_destroy(lower_info *li);

void* amf_info_write(uint32_t ppa, uint32_t size, value_set *value,algo_req * const req);
void* amf_info_read(uint32_t ppa, uint32_t size, value_set *value,algo_req * const req);

void *amf_info_write_sync(uint32_t type, uint32_t ppa, char *data);
void *amf_info_read_sync(uint32_t type, uint32_t ppa, char *data);

void* amf_info_device_badblock_checker(uint32_t ppa,uint32_t size,void *(*process)(uint64_t, uint8_t));
void* amf_info_trim_block(uint32_t ppa);
void* amf_info_trim_a_block(uint32_t ppa);
void* amf_info_refresh(struct lower_info*);
void amf_info_stop();
void amf_info_show_info();
uint32_t amf_info_lower_tag_num();
void amf_flying_req_wait();
uint32_t amf_info_dump(lower_info*li, FILE *fp);
uint32_t amf_info_load(lower_info *li, FILE *fp);
#endif
