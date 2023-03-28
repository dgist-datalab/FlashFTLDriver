#ifndef POSIX_HEADER
#define POSIX_HEADER
#include "../../include/container.h"
#include "../../include/sem_lock.h"

//#define LASYNC
#define FS_LOWER_W 1
#define FS_LOWER_R 2
#define FS_LOWER_T 3

uint32_t posix_create(lower_info*,blockmanager *);
void *posix_destroy(lower_info*);
void* posix_write(uint32_t ppa, uint32_t size, value_set *value, algo_req * const req);
void* posix_read(uint32_t ppa, uint32_t size, value_set* value,algo_req * const req);

void *posix_write_sync(uint32_t type, uint32_t ppa, char *data);
void *posix_read_sync(uint32_t type, uint32_t ppa, char *data);

void* posix_make_write(uint32_t ppa, uint32_t size, value_set *value, algo_req * const req);
void* posix_make_read(uint32_t ppa, uint32_t size, value_set *value, algo_req * const req);
void* posix_badblock_checker(uint32_t ppa, uint32_t size, void*(*process)(uint64_t,uint8_t));
void* posix_trim_block(uint32_t ppa);
void* posix_trim_a_block(uint32_t ppa);
void *posix_make_trim(uint32_t ppa);
void *posix_refresh(lower_info*);
void posix_stop();
void posix_flying_req_wait();
uint32_t convert_ppa(uint32_t);
uint32_t posix_dump(lower_info *li, FILE *p);
uint32_t posix_load(lower_info *li, FILE *p);


typedef struct posix_request {
	FSTYPE type;
	uint32_t key;
	value_set *value;
	char *data;
	algo_req *upper_req;
	uint32_t size;
	bool isAsync;
	fdriver_lock_t lock;
}posix_request;

typedef struct mem_seg {
	char * storage;
	char oob[OOB_SIZE];
} mem_seg;
#endif
