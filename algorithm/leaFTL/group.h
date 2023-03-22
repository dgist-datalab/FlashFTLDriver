#pragma once
#include "./CRB.h"
#include "./PLR_segment.h"
#include "../../include/container.h"
#include "../../include/settings.h"
#include "../../interface/interface.h"
#include "./issue_io.h"
#define FAST_LOAD_STORE
#define NOT_FOUND UINT32_MAX
#define INITIAL_STATE_PADDR (UINT32_MAX)
#define MAPINTRANS ((PAGESIZE/sizeof(uint32_t)))
#define TRANSMAPNUM ((SHOWINGSIZE/PAGESIZE*L2PGAP)/(MAPINTRANS))

//typedef std::map<uint32_t, segment*> level;
//typedef std::map<uint32_t, segment*>::iterator level_iter;
typedef std::vector<segment*> level;
typedef std::vector<segment*>::iterator level_iter;
typedef std::vector<level*>::iterator level_list_iter;

enum RETRY_FLAG{
	INIT, NOT_RETRY, NORMAL_RETRY, FORCE_RETRY, DATA_FOUND
};

enum CACHE_FLAG{
    UNCACHED, CACHED, FLYING
};

enum GRP_TYPE{
    GRP_TYPE_NONE, GP_READ, GP_WRITE
};

enum GRP_READ_TYPE{
    GRP_READ_TYPE_NONE, DATAREAD, NOTDATAREADSTART, MAPREAD
};

struct group_read_param;
typedef std::list<group_read_param*>::iterator pending_iter;
typedef std::vector<level*> level_list_t;

typedef struct group{
    level_list_t* level_list;
    CRB *crb;
    uint32_t max_height;
    uint32_t map_idx;
    uint32_t ppa;

    /*Cache flag*/
    uint64_t size; //byte
    CACHE_FLAG cache_flag;
    std::list<group_read_param*> pending_request;
    bool isclean;
    void *lru_node;
}group;

typedef struct group_monitor{
    uint64_t total_segment;
    uint32_t interval_segment;
    uint64_t compaction_cnt;
}group_monitor;

typedef struct group_read_param{
    /*COMMON flags*/
    group *gp;
    bool read_done;
    bool user_pass_value;
    GRP_READ_TYPE r_type;
    request *user_req;

    /*CACHE flags*/
    GRP_TYPE path_type;

    /*DATA flags*/
    RETRY_FLAG retry_flag;

    /*target value*/
    value_set *value; //DATA or mapping data
    segment *seg;
    uint32_t *oob;
    uint32_t lba;
    uint32_t set_idx;
    uint32_t piece_ppa;
}group_read_param;


void group_init(group *gp, uint32_t idx);
bool group_get(group *gp, uint32_t lba, group_read_param* grp, bool issuerreq, GRP_TYPE path_type);
uint32_t group_oob_check(group_read_param *grp);
void *group_param_free(group_read_param *);
void group_insert(group *gp, temp_map *tmap, SEGMENT_TYPE type, int32_t interval, void (*cache_size_update)(group *gp, uint32_t size, bool decrease));
void group_to_translation_map(group *gp, char *des);
void group_free(group *gp);
void group_get_exact_piece_ppa(group *gp, uint32_t lba, uint32_t set_idx, group_read_param *grp, bool isstart, lower_info *li, void (*cache_insert)(group *gp, uint32_t *piece_ppa));
bool group_get_map_read_grp(group *gp, bool isstart, group_read_param *grp, bool iswrite_path, bool isuserreq, void(*cache_insert)(group *gp, uint32_t *piece_ppa));
void group_from_translation_map(group *gp, uint32_t *lba, uint32_t *piece_ppa, uint32_t idx);
void group_clean(group *gp, bool reinit, bool byecivtion);
void group_monitor_print();
void group_load_levellist(group *gp);
void group_store_levellist(group *gp);
static inline group_read_param *group_get_empty_grp(){
    group_read_param *res=(group_read_param*)calloc(1, sizeof(group_read_param));
    return res;
}

static inline bool group_cached(group *gp){
    if(gp->cache_flag==CACHE_FLAG::CACHED){
        return true;
    }
    return false;
}