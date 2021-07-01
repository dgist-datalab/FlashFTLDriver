#include "../../include/settings.h"
#include "../../include/container.h"
#include <map>
typedef struct page_param{
	request *parents;
	uint32_t address;
	value_set *value;
}page_param;

typedef struct align_buffer{
	uint8_t idx;
	value_set *value[L2PGAP];
	KEYT key[L2PGAP];
}align_buffer;

#if 1 //NAM
typedef struct pNode{ 
	request *req; 
	KEYT key; 
	value_set *value; 
	struct pNode *next;
}pNode;

typedef struct bucket{ 
	struct pNode *head;
	uint32_t idx; 
	uint16_t count; 
	uint64_t heap_idx; 
}bucket;

typedef struct dawid_buffer{ 
	uint32_t tot_count; 
	struct bucket* htable; 
}dawid_buffer; 

typedef struct node_dirty{ 
	uint32_t idx; 
	node_dirty *next; 
}node_dirty;

typedef struct element{ 
	uint32_t idx; 
	uint16_t count; 
}element; 

typedef struct HeapType{ 
	element* heap; 
	uint16_t heap_size; 
}ht; 
#endif

typedef struct page_read_buffer{
	std::multimap<uint32_t, algo_req *> * pending_req;
	std::multimap<uint32_t, algo_req *>* issue_req;
	fdriver_lock_t pending_lock;
	fdriver_lock_t read_buffer_lock;
	uint32_t buffer_ppa;
	char buffer_value[PAGESIZE];
}page_read_buffer;

uint32_t page_create (lower_info*,blockmanager *, algorithm *);
void page_destroy (lower_info*,  algorithm *);
uint32_t page_argument(int argc, char **argv);
uint32_t page_read(request *const);
uint32_t page_write(request *const);
uint32_t page_remove(request *const);
uint32_t page_flush(request *const);
void *page_end_req(algo_req*);
#if 1 //NAM
inline void send_user_req(request *const req, uint32_t type, ppa_t ppa, value_set *value);
uint32_t align_buffering(request *const req, KEYT key, value_set *value);

uint32_t find_idx(KEYT); 
uint8_t is_page_dirty(uint32_t); 
int32_t dawid_buffering(request *const, KEYT, value_set); 
int32_t dequeue_and_flush(uint32_t); 
pNode* get_node(request *, KEYT, value_set); 
void pageIsDirty(uint32_t); 
void pageIsClean(uint32_t); 
void insert_max_heap(element); 
void search_heap(uint32_t); 
void delete_max_heap(void); 
#endif
