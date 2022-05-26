#ifndef __H_CONTAINER__
#define __H_CONTAINER__
 
#include "settings.h"
#include "types.h"
#include "utils.h"
#include "sem_lock.h"
#include <stdarg.h>
#include <pthread.h>

typedef struct lower_info lower_info;
typedef struct algorithm algorithm;
typedef struct algo_req algo_req;
typedef struct request request;
typedef struct blockmanager blockmanager;

typedef struct upper_request{
	const FSTYPE type;
	const KEYT key;
	uint32_t length;
	char* value;
	//anything
}upper_request;

typedef struct value_set{
	char* value;
	uint32_t length;
	int dmatag; //-1 == not dma_alloc, others== dma_alloc
	uint32_t ppa;
	bool from_app;
	char* rmw_value;
	uint8_t status;
	uint32_t len;
	uint32_t offset;
	char oob[OOB_SIZE];
}value_set;


typedef struct vectored_request{
	uint32_t tag_id;
	uint32_t type;
	uint32_t seq_id;
	uint32_t size;
	uint32_t done_cnt;
	uint32_t tid;
	char* buf;

	request *req_array;
	uint32_t mark;
	void* (*end_req)(void*);
	void* origin_req;
	MeasureTime latency_checker;
} vec_request;

struct request {
	FSTYPE type;
	KEYT key;
	bool retry;
	uint32_t tag_num;
	uint32_t offset;
	uint32_t tid;
	uint32_t length;
	char *buf;
	
	uint32_t crc_value;
	uint64_t ppa;/*it can be the iter_idx*/
	uint32_t seq;
	uint32_t global_seq;
#ifdef hash_dftl
	volatile int num; /*length of requests*/
	volatile int cpl; /*number of completed requests*/
#endif
	int not_found_cnt;
	value_set *value;
	//value_set **multi_value;
	char **app_result;

	//KEYT *multi_key;
	bool (*end_req)(struct request *const);
	void *(*special_func)(void *);
	bool (*added_end_req)(struct request *const);
	bool flush_all;
	uint8_t magic;
	void *param;
	void *__hash_node;
	//pthread_mutex_t async_mutex;
	int mark;
	bool is_sequential_start;
	uint32_t consecutive_length; 
	uint32_t round_cnt;

/*s:for application req
	char *target_buf;
	uint32_t inter_offset;
	uint32_t target_len;
	char istophalf;
	FSTYPE org_type;
e:for application req*/

	uint8_t type_ftl;
	uint8_t type_lower;
	uint8_t buffer_hit;

	uint8_t before_type_lower;
	bool isstart;
	MeasureTime latency_checker;

	bool mapping_cpu_check;
	MeasureTime mapping_cpu;

	fdriver_lock_t done_lock;
	bool write_done;
	bool map_done;
	/* HASH_KVSSD */
#ifdef hash_dftl
	void *hash_param;
#endif
	struct vectored_request *parents;
};

struct algo_req{
	uint32_t ppa;
	uint32_t test_ppa; //for lower layer
	request * parents;
	MeasureTime latency_lower;
	uint8_t type;
	value_set *value;
	bool rapid;
	uint8_t type_lower;
	//0: normal, 1 : no tag, 2: read delay 4:write delay
	void *(*end_req)(struct algo_req *const);
	void *param;
};

struct lower_info {
	uint32_t (*create)(struct lower_info*, blockmanager *bm);
	void* (*destroy)(struct lower_info*);
	void* (*write)(uint32_t ppa, uint32_t size, value_set *value, algo_req * const req);
	void* (*read)(uint32_t ppa, uint32_t size, value_set *value, algo_req * const req);
	void* (*write_sync)(uint32_t type, uint32_t ppa, char *data);
	void* (*read_sync)(uint32_t type, uint32_t ppa, char *data);

	void* (*device_badblock_checker)(uint32_t ppa,uint32_t size,void *(*process)(uint64_t, uint8_t));
	void* (*trim_block)(uint32_t ppa);
	void* (*trim_a_block)(uint32_t ppa);
	void* (*refresh)(struct lower_info*);
	void (*stop)();
	int (*lower_alloc) (int type, char** buf);
	void (*lower_free) (int type, int dmaTag);
	void (*lower_flying_req_wait) ();
	void (*lower_show_info)();
	uint32_t (*lower_tag_num)();
	void (*print_traffic)(struct lower_info *);
	uint32_t (*dump)(lower_info *li, FILE *fp);
	uint32_t (*load)(lower_info *li, FILE *fp );
	struct blockmanager *bm;

	lower_status (*statusOfblock)(BLOCKT);
	
	uint64_t write_op;
	uint64_t read_op;
	uint64_t trim_op;

	uint32_t NOB;
	uint32_t NOP;
	uint32_t SOK;
	uint32_t SOB;
	uint32_t SOP;
	uint32_t PPB;
	uint32_t PPS;
	uint64_t TS;
	uint64_t DEV_SIZE;//for sacle up test
	uint64_t all_pages_in_dev;//for scale up test

	uint64_t req_type_cnt[LREQ_TYPE_NUM];
	void *private_data;
	//anything
};

static bool collect_io_type(uint32_t type, lower_info *li){
	if(type < LREQ_TYPE_NUM){
		li->req_type_cnt[type]++;
	
#ifdef METAONLY
		switch(type){
			case GCDR:
			case GCDW:
			case DATAR:
			case DATAW:
			case COMPACTIONDATAR:
			case COMPACTIONDATAW:
				return false;
			default:
				return true;
		}
#else
		return true;
#endif
	}
	return false;
}

struct algorithm{
	/*interface*/
	uint32_t (*argument_set) (int argc, char**argv);
	uint32_t (*create) (lower_info*, blockmanager *bm, struct algorithm *);
	void (*destroy) (lower_info*, struct algorithm *);
	uint32_t (*read)(request *const);
	uint32_t (*write)(request *const);
	uint32_t (*flush)(request *const);
	uint32_t (*remove)(request *const);
	uint32_t (*test)();
	uint32_t (*print_log)();
	uint32_t (*empty_cache)();
	void (*dump_prepare)();
	uint32_t (*dump)(FILE *fp);
	uint32_t (*load)(lower_info *li, blockmanager *bm, struct algorithm *, FILE *fp);
	lower_info* li;
	struct blockmanager *bm;
	void *algo_body;
};

typedef struct __OOBT{
	char d[OOB_SIZE];
}__OOB;

typedef struct masterblock{
	uint32_t block_idx;
	uint16_t now_assigned_pptr;
	uint8_t bitset[_PPB*L2PGAP/8];
	__OOB oob_list[_PPB];
	bool is_full_invalid;
	uint16_t invalidate_piece_num;
	uint16_t validate_piece_num;
	uint32_t punit_num;
//	uint32_t seg_idx;
//	void *hptr;
	void *private_data;
}__block;

typedef struct mastersegment{
	uint32_t seg_idx;
	__block* blocks[BPS];
	uint16_t now_assigned_bptr;
	uint32_t used_page_num;
	uint32_t validate_piece_num;
	uint32_t invalidate_piece_num;
	uint32_t invalid_block_num;
	void *private_data;
}__segment;

typedef struct ghostsegment{ //for gc
	bool all_invalid;
	__block* blocks[BPS];
	uint32_t seg_idx;
	uint32_t invalidate_piece_num;
	uint32_t validate_piece_num;
}__gsegment;

enum{
	BLOCK_RESERVE, BLOCK_ACTIVE, BLOCK_LOAD,
};

struct blockmanager{
	uint32_t type;
	__segment* (*get_segment)		(struct blockmanager*, uint32_t type);
	__segment* (*pick_segment)		(struct blockmanager *, uint32_t seg_idx, uint32_t type); //used for load
	int32_t (*get_page_addr)			(__segment*); 
	int32_t (*pick_page_addr)		(__segment*);
	bool (*check_full)				(__segment*);
	bool (*is_gc_needed)			(struct blockmanager*);
	__gsegment* (*get_gc_target)	(struct blockmanager*);
	void (*trim_segment)			(struct blockmanager*, __gsegment*);

	int (*bit_set)		(struct blockmanager*, uint32_t piece_ppa);
	int (*bit_unset)	(struct blockmanager*, uint32_t piece_ppa);
	bool (*bit_query)		(struct blockmanager *, uint32_t piece_ppa);
	bool (*is_invalid_piece) (struct blockmanager *, uint32_t piece_ppa);

	void (*set_oob) (struct blockmanager *, char* data, int len, uint32_t ppa);
	char *(*get_oob)(struct blockmanager *, uint32_t ppa);
	void (*change_reserve_to_active)(struct blockmanager *, __segment *reserve);
	void (*insert_gc_target)(struct blockmanager *, uint32_t seg_idx);
	uint32_t (*total_free_page_num)(struct blockmanager *, __segment *active); 
	uint32_t (*seg_invalidate_piece_num)(struct blockmanager *, uint32_t seg_idx);
	uint32_t (*invalidate_seg_num)(struct blockmanager *);

	uint32_t (*load)(struct blockmanager *, FILE * fp);
	uint32_t (*dump)(struct blockmanager *, FILE* fp);

	lower_info *li;
	void *private_data;
};

#define SEGNUM(ppa)  (((ppa)/L2PGAP)/_PPS)
#define SEGOFFSET(ppa) (((ppa/L2PGAP))%_PPS)
#define SEGPIECEOFFSET(ppa) (((ppa))%(_PPS*L2PGAP))
#define BLOCKNUM(ppa)  ((((ppa)/L2PGAP)%_PPS)/_PPB)


#define for_each_block(segs,block,idx)\
	for(idx=0,block=segs->blocks[idx];idx<BPS; ++idx, block=segs->blocks[idx!=BPS?idx:BPS-1])

#ifdef sequential
	#define PPAMAKER(bl,idx) ((bl)->block_idx*_PPB+idx)
	#define for_each_page_in_seg(segs,page,bidx,pidx)\
		for(bidx=0; bidx<BPS; bidx++)\
			for(pidx=0, page=PPAMAKER(segs->blocks[bidx],pidx); pidx<_PPB; pidx++, page=PPAMAKER(segs->blocks[bidx],pidx))

	#define for_each_page_in_seg_blocks(segs,block,page,bidx,pidx)\
		for(bidx=0, block=segs->blocks[bidx]; bidx<BPS; bidx++, block=segs->blocks[(bidx!=BPS?bidx:BPS-1)])\
			for(pidx=0, page=PPAMAKER(segs->blocks[bidx],pidx); pidx<_PPB; pidx++, page=PPAMAKER(segs->blocks[bidx],pidx))
		
#else
	#define PPAMAKER(bl,idx) ((bl)->punit_num)+(idx<<6)+((bl)->block_idx)
	#define for_each_page_in_seg(segs,page,bidx,pidx)\
		for(pidx=0;pidx<_PPB; pidx++)\
			for(bidx=0,page=PPAMAKER(segs->blocks[bidx],pidx); bidx<BPS; bidx++,page=PPAMAKER(segs->blocks[(bidx!=BPS?bidx:BPS-1)],pidx))


	#define for_each_page_in_seg_blocks(segs,block,page,bidx,pidx)\
		for(pidx=0;pidx<_PPB; pidx++)\
			for(bidx=0,block=segs->blocks[bidx],page=PPAMAKER(segs->blocks[bidx],pidx); bidx<BPS; bidx++,page=PPAMAKER(segs->blocks[(bidx!=BPS?bidx:BPS-1)],pidx),block=segs->blocks[(bidx!=BPS?bidx:BPS-1)])
#endif



#endif
