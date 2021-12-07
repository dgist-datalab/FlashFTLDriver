#include "../include/settings.h"
#include "../include/container.h"
#define LOWER_MEM_DEV
#define REAL_PAGE_SIZE 8192
#define R2PGAP (PAGESIZE/REAL_PAGE_SIZE)

uint32_t	pu_create(lower_info *li, blockmanager *bm);
void*		pu_destroy(lower_info *li);
void*		pu_write(uint32_t ppa, uint32_t size, value_set *value,algo_req * const req);
void* 		pu_read(uint32_t ppa, uint32_t size, value_set *value, algo_req * const req);
void *		pu_write_sync(uint32_t type, uint32_t ppa, char *data);
void *		pu_read_sync(uint32_t type, uint32_t ppa, char *data);
void* 		pu_device_badblock_checker(uint32_t ppa,uint32_t size,void *(*process)(uint64_t, uint8_t));
void* 		pu_trim_block(uint32_t ppa);
//void* 		pu_trim_a_block(uint32_t ppa,bool async);
void* 		pu_refresh(struct lower_info*);
void		pu_stop();
void		pu_show_info();
uint32_t	pu_lower_tag_num();
void		pu_flying_req_wait();
void		pu_traffic_print(lower_info *);
uint32_t	pu_dump(lower_info *li, FILE *fp);
uint32_t	pu_load(lower_info *li, FILE *fp);
