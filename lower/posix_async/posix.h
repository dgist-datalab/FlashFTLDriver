#include "../../include/container.h"

#define FS_LOWER_W 1
#define FS_LOWER_R 2
#define FS_LOWER_T 3

uint32_t posix_create(lower_info*, blockmanager *);
void *posix_destroy(lower_info*);
void* posix_push_data(ppa_t ppa, uint32_t size, value_set *value,bool async, algo_req * const req);
void* posix_pull_data(ppa_t ppa, uint32_t size, value_set* value,bool async,algo_req * const req);
void* posix_make_push(ppa_t ppa, uint32_t size, value_set *value,bool async, algo_req * const req);
void* posix_make_pull(ppa_t ppa, uint32_t size, value_set *value,bool async, algo_req * const req);
void* posix_badblock_checker(ppa_t ppa, uint32_t size, void*(*process)(uint64_t,uint8_t));
void* posix_trim_block(ppa_t ppa,bool async);
void *posix_make_trim(ppa_t ppa, bool async);
void *posix_refresh(lower_info*);
void posix_stop();
void posix_flying_req_wait();
void* posix_trim_a_block(uint32_t PPA, bool async);
typedef struct posix_request {
	FSTYPE type;
	ppa_t key;
	value_set *value;
	algo_req *upper_req;
	bool isAsync;
	uint32_t size;
}posix_request;
