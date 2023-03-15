#pragma once
#include "./CRB.h"
#include "./PLR_segment.h"
#include "../../include/settings.h"
#include "../../interface/interface.h"
#include "./issue_io.h"
#define NOT_FOUND UINT32_MAX
#define INITIAL_STATE_PADDR (UINT32_MAX)
#define MAPINTRANS ((PAGESIZE/sizeof(uint32_t)))
#define TRANSMAPNUM ((_NOP*L2PGAP)/(MAPINTRANS))

//typedef std::map<uint32_t, segment*> level;
//typedef std::map<uint32_t, segment*>::iterator level_iter;
typedef std::vector<segment*> level;
typedef std::vector<segment*>::iterator level_iter;
typedef std::vector<level*>::iterator level_list_iter;

enum{
	NOT_RETRY, NORMAL_RETRY, FORCE_RETRY, DATA_FOUND
};

enum CACHE_FLAG{
    UNCACHED, CACHED, FLYING
};

enum GRP_TYPE{
    GP_READ, GP_WRITE
};

struct group_read_param;
typedef struct group{
    std::vector<level*>* level_list;
    CRB *crb;
    uint32_t max_height;
    uint32_t map_idx;
    uint32_t ppa;

    uint64_t size; //byte
    CACHE_FLAG cache_flag;
    std::list<group_read_param*> pending_request;
    void *lru_node;
}group;

typedef struct group_monitor{
    uint64_t total_segment;
    uint32_t interval_segment;
    uint64_t compaction_cnt;
}group_monitor;

typedef struct group_read_param{
    GRP_TYPE type;
    segment *seg;
    uint32_t *oob;
    group *gp;
    uint32_t lba;

    uint32_t piece_ppa;
    bool read_done;
    uint32_t retry_flag;
    uint32_t set_idx;

    value_set *value;
}group_read_param;

void group_init(group *gp, uint32_t idx);
group_read_param *group_get(group *gp, uint32_t lba, request *req);
uint32_t group_get_retry(uint32_t lba, group_read_param *grp, request *req, void (*cache_insert)(group *gp, uint32_t *piece_ppa));
uint32_t group_oob_check(group_read_param *grp);
void *group_param_free(group_read_param *);
void group_insert(group *gp, temp_map *tmap, SEGMENT_TYPE type, int32_t interval);
void group_to_translation_map(group *gp, char *des);
void group_free(group *gp);
void group_get_exact_piece_ppa(group *gp, uint32_t lba, uint32_t set_idx, group_read_param *grp, bool isstart, lower_info *li, void (*cache_insert)(group *gp, uint32_t *piece_ppa));

void group_read_from_NAND(group *gp, bool isstart, group_read_param *grp, lower_info *li, void(*cache_insert)(group *gp, uint32_t *piece_ppa));

void group_from_translation_map(group *gp, uint32_t *lba, uint32_t *piece_ppa, uint32_t idx);
void group_clean(group *gp, bool reinit);
void group_monitor_print();