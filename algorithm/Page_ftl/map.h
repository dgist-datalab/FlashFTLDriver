#include "../../include/container.h"
#include "../../interface/interface.h"
//#define MAX_PROTECTED _DCE/100
#define MAX_PROTECTED 3 

#if 0
typedef struct page_map_body{
	uint32_t *mapping;
	bool isfull;
	uint32_t assign_page;

	/*segment is a kind of Physical Block*/
	__segment *reserve; //for gc
	__segment *active; //for gc
}pm_body;
#endif

#if 1 //NAM
typedef struct dirty_page_list{
	uint32_t idx; 
	struct dirty_page_list *next; 
}dp_list; 

typedef struct page_map_body{ 
	uint32_t *mapping; 
	uint32_t *meta_mapping; 
	unsigned char *dirty_check; //checked mapping table's dirty condition  
	struct dirty_page_list *dirty_check_list; 
	uint32_t heap_index; 
	uint32_t tot_dirty_pages; 
	uint32_t tot_flush_count; 
	bool isfull; 
	uint32_t assign_page; 

	/*segment is a kind of Physical Block*/ 
	__segment *reserve; //for gc
	__segment *active; //for gc
	//__segment *mapflush; //for map
	__segment *map_reserve; 
	__segment *map_active; 
}pm_body; 
#endif

void page_map_create();
#if 1 //NAM
int32_t page_dMap_check(KEYT lba); 
int32_t page_map_flush(); 
#endif
uint32_t page_map_assign(KEYT *lba, uint32_t max_idx);
uint32_t page_map_pick(uint32_t lba);
uint32_t page_map_trim(uint32_t lba);
uint32_t page_map_gc_update(KEYT* lba, uint32_t idx);
uint32_t page_meta_map_gc_update(KEYT* lba, uint32_t idx);
void page_map_free();
void page_map_dirtyCheck_list(uint32_t idx);
