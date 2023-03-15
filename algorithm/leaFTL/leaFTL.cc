#include "leaFTL.h"
#include "./write_buffer.h"
#include "./page_manager.h"
#include "./lea_container.h"
#include "./issue_io.h"
#include "../../include/debug_utils.h"
#include "group.h"
#include "../../data_struct/lru_list.h"
#include <vector>

#ifdef DEBUG
uint32_t *exact_map;
#endif

lea_write_buffer *wb;
page_manager *pm;
page_manager *translate_pm;
blockmanager *lea_bm;
lower_info *lower;
temp_map *user_temp_map;
temp_map *gc_temp_map;
temp_map *compaction_temp_map;
LRU *gp_lru;
uint64_t lru_max_byte;
uint64_t lru_now_byte;
uint64_t lru_reserve_byte;
uint32_t max_cached_trans_map;

typedef struct group_update_param{
   group_read_param grp;
   uint32_t size;
   temp_map map;
   group *gp;
}group_update_param;

group_update_param gup_set[TRANSMAPNUM];

group main_gp[TRANSMAPNUM];

extern uint32_t test_key;
extern uint32_t lea_test_piece_ppa;
#define WRITE_BUF_SIZE (32*1024*1024)
#define INITIAL_STATE_PADDR (UINT32_MAX)

struct algorithm lea_FTL={
    .argument_set=lea_argument,
    .create=lea_create,
    .destroy=lea_destroy,
    .read=lea_read,
    .write=lea_write,
    .flush=NULL,
    .remove=lea_remove,
    .test=NULL,
    .print_log=NULL,
    .empty_cache=NULL,
    .dump_prepare=NULL,
    .dump=NULL,
    .load=NULL,
};

typedef struct user_io_param{
    uint32_t type;
    uint32_t piece_ppa;
    value_set *value;
}user_io_param;

user_io_param* get_user_io_param(uint32_t type, uint32_t piece_ppa, value_set *value){
    user_io_param *res=(user_io_param*)malloc(sizeof(user_io_param));
    res->type=type;
    res->piece_ppa=piece_ppa;
    res->value=value;
    return res;
}

uint32_t lea_create(lower_info *li, blockmanager *bm, algorithm *algo){
    algo->li=li;
    algo->bm=bm;
    lower=li;
    lea_bm=bm;

    exact_map=(uint32_t*)malloc(sizeof(uint32_t) * _NOP * L2PGAP);
    memset(exact_map, -1, sizeof(uint32_t)*_NOP*L2PGAP);

    wb=lea_write_buffer_init(WRITE_BUF_SIZE);
    uint32_t sector_num=WRITE_BUF_SIZE/L2PGAP;
    user_temp_map=temp_map_assign(sector_num);
    temp_map_clear(user_temp_map);

    sector_num=_PPS*L2PGAP;
    gc_temp_map=temp_map_assign(sector_num);
    temp_map_clear(gc_temp_map);

    sector_num=MAPINTRANS;
    compaction_temp_map=temp_map_assign(sector_num);

    pm=pm_init(li, bm);
    translate_pm=pm_init(li, bm);
    lru_init(&gp_lru, NULL, NULL);
    max_cached_trans_map=TRANSMAPNUM*30/100;
    lru_max_byte=max_cached_trans_map*PAGESIZE;
    lru_now_byte=0;
    lru_reserve_byte=0;
    
    for(uint32_t i=0; i<TRANSMAPNUM; i++){
        group_init(&main_gp[i], i);
    }
    return 1;
}

void lea_destroy(lower_info *, algorithm *){
    lea_write_buffer_free(wb);
    pm_free(pm);
    free(exact_map);
    temp_map_free(user_temp_map);
    temp_map_free(gc_temp_map);
    temp_map_free(compaction_temp_map);
}

uint32_t lea_argument(int argc, char **argv){
    return 1;
}

void *lea_end_req(algo_req* const req){
    group_read_param *grp=(group_read_param*)req->param;
    grp->read_done=true;
    bool data_found=false;
    grp->oob=(uint32_t*)lea_bm->get_oob(lea_bm, grp->piece_ppa/L2PGAP);
    switch(grp->retry_flag){
        case NORMAL_RETRY:
        case NOT_RETRY:
            grp->set_idx=group_oob_check(grp);
            if(grp->set_idx==UINT32_MAX){
                //retry
                if(grp->retry_flag==NORMAL_RETRY){
                    printf("not found %u", grp->lba);
                    printf(", grp info: piece_ppa(%u) set_idx(%u)\n", grp->piece_ppa, grp->set_idx);
                    abort();
                }
            }
            else{
                data_found=true;
            }
            break;
        case DATA_FOUND:
            data_found=true;
            break;
    }

    if(data_found){
        memmove(grp->value->value, &grp->value->value[(grp->set_idx)*LPAGESIZE], LPAGESIZE);
        req->parents->end_req(req->parents);
        free(grp);
    }
    else{
        inf_assign_try(req->parents);
    }

    free(req);
    return NULL;
}

uint32_t lea_read(request *const req){
    group_read_param *grp;
    if(req->param){ //retry
        grp=(group_read_param*)req->param;
        group_get_retry(req->key, grp, req, lea_cache_insert);
    }
    else{
        char *data = lea_write_buffer_get(wb, req->key);
        if (!data){
            group *gp=&main_gp[req->key/MAPINTRANS];
            lea_cache_evict(gp->size);
            grp=group_get(&main_gp[req->key/MAPINTRANS], req->key, req);
            if(grp==NULL){
                req->type=FS_NOTFOUND_T;
                return 0;
            }
        }
        else{
            memcpy(req->value->value, data, LPAGESIZE);
            req->end_req(req);
            return 1;
        }
    }
    return 1;
}

#define ASSIGN_FLUSH_MAP(_TMAP, _SIZE, _LBA, _PIECEPPA)\
    do{\
        (_TMAP)->size=(_SIZE);\
        (_TMAP)->lba=(_LBA);\
        (_TMAP)->piece_ppa=(_PIECEPPA);\
    }while(0)

static inline void wrapper_group_insert(group *gp, uint32_t size, uint32_t *lba, uint32_t *piece_ppa, bool isacc, int32_t interval){
    temp_map flush_map;
    ASSIGN_FLUSH_MAP(&flush_map, size, lba, piece_ppa);
    if(size <=2){
        group_insert(gp, &flush_map, SEGMENT_TYPE::ACCURATE, size==1?0:lba[1]-lba[0]);
    }
    else{
        if(isacc){
            if(interval==-1){
                printf("error %s:%u, interval must be available!\n", __FUNCTION__, __LINE__);
                abort();
            }
            group_insert(gp, &flush_map, SEGMENT_TYPE::ACCURATE, interval);
        }
        else{
            group_insert(gp, &flush_map, SEGMENT_TYPE::APPROXIMATE, -1);
        }
    }
}

void lea_making_segment_per_gp(group *gp, uint32_t *lba, uint32_t *piece_ppa, int32_t *interval, uint32_t size){
    if(size==1){
        wrapper_group_insert(gp, size, lba, piece_ppa, false, 0);
        return;
    }
    uint32_t app_start_idx=UINT32_MAX;
    uint32_t app_length=0;

    uint32_t acc_start_idx=UINT32_MAX;
    uint32_t acc_length;
    uint32_t acc_interval;
    for(uint32_t i=1; i<size; i++){
        if(acc_start_idx==UINT32_MAX){
            acc_start_idx=i-1;
            acc_length=2;
            acc_interval=interval[i-1];
        }
        else{
            if(acc_interval==interval[i-1]){
                acc_length++;
                continue;
            }
            else{
                if(acc_length < 3){
                    if(app_start_idx==UINT32_MAX){
                        app_start_idx=acc_start_idx;
                    }
                    app_length+=acc_length;
                }
                else{
                    //flush previous acc & app segment 
                    if(app_start_idx!=UINT32_MAX){
                        wrapper_group_insert(gp, app_length, &lba[app_start_idx], &piece_ppa[app_start_idx], false, UINT32_MAX);
                        app_start_idx=UINT32_MAX;
                        app_length=0;
                    }
                    //clear information
                    wrapper_group_insert(gp, acc_length, &lba[acc_start_idx], &piece_ppa[acc_start_idx], true, acc_interval);
                }

                acc_start_idx=i;
                acc_length=1;
                acc_interval=UINT32_MAX;
            }
        }
    }

    if(acc_length < 3){
        if(app_start_idx==UINT32_MAX){
            app_start_idx=acc_start_idx;
        }
        app_length+=acc_length;
        acc_start_idx=UINT32_MAX;
    }

    if(app_start_idx!=UINT32_MAX){
        wrapper_group_insert(gp, app_length, &lba[app_start_idx], &piece_ppa[app_start_idx], false, UINT32_MAX);
    }

    if(acc_start_idx!=UINT32_MAX){
        wrapper_group_insert(gp, acc_length, &lba[acc_start_idx], &piece_ppa[acc_start_idx], true, acc_interval);
    }
}

void lea_mapping_find_exact_piece_ppa(temp_map *map, bool invalidate, blockmanager *bm){
    std::list<group_read_param*> grp_list;
    group_read_param *grp=(group_read_param*)malloc(sizeof(group_read_param)*map->size);
    /*do round*/
    for(uint32_t i=0; i<map->size; i++){
        if(map->lba[i]==test_key){
            //GDB_MAKE_BREAKPOINT;
        }
        group *gp=&main_gp[map->lba[i]/MAPINTRANS];
        if(gp->cache_flag==CACHE_FLAG::UNCACHED){
            /*eviction*/
            lea_cache_evict(gp->size);
        }
        group_get_exact_piece_ppa(gp, map->lba[i], i, &grp[i], true, lower, lea_cache_insert);
        grp_list.push_back(&grp[i]);
    }

    while(grp_list.size()){
        std::list<group_read_param *>::iterator list_iter = grp_list.begin();
        for (; list_iter != grp_list.end();){
            group_read_param *t_grp=*list_iter;
            if(t_grp->lba==test_key){
                //GDB_MAKE_BREAKPOINT;
            }

            if(t_grp->gp->cache_flag==CACHE_FLAG::FLYING){
                if(t_grp->read_done){
                    /*header request*/
                    group_get_exact_piece_ppa(t_grp->gp, t_grp->lba, t_grp->set_idx, t_grp, false, lower, lea_cache_insert);
                }
                else{
                    list_iter++;
                    continue;
                }
            }

            if(t_grp->retry_flag==DATA_FOUND){
                #ifdef DEBUG
                if(t_grp->piece_ppa!=exact_map[t_grp->lba]){
                    printf("address error!\n");
                    GDB_MAKE_BREAKPOINT;
                }
                #endif
               if(invalidate && t_grp->piece_ppa!=INITIAL_STATE_PADDR){
                    invalidate_piece_ppa(bm, t_grp->piece_ppa);
                }
                else if(invalidate==false){
                    map->piece_ppa[t_grp->lba%MAPINTRANS]=t_grp->piece_ppa;
                }
                grp_list.erase(list_iter++);
            }
            else if(t_grp->read_done==false){
                list_iter++;
            }
            else{
                group_get_exact_piece_ppa(t_grp->gp, t_grp->lba, t_grp->set_idx, t_grp, false, lower, lea_cache_insert);
                list_iter++;
            }
        }
    }      
    free(grp);
}

void lea_compaction(uint32_t idx){
    group *target_gp=&main_gp[idx];
    if(target_gp->level_list->size()<=1) return;
    
    for(uint32_t i=0; i<MAPINTRANS; i++){
        compaction_temp_map->lba[i]=idx*MAPINTRANS+i;
    }
    lea_mapping_find_exact_piece_ppa(compaction_temp_map, false, NULL);
    group_from_translation_map(target_gp, compaction_temp_map->lba, compaction_temp_map->piece_ppa, idx);
}

void lea_gup_init(group_update_param *t_gup, group *gp, temp_map *map, uint32_t start_ptr, uint32_t num){
    t_gup->gp = gp;
    t_gup->map.lba = &map->lba[start_ptr];
    t_gup->map.piece_ppa = &map->piece_ppa[start_ptr];
    t_gup->map.interval= &map->interval[start_ptr];
    t_gup->size = num;
    lea_cache_evict(gp->size);
    group_read_from_NAND(gp, true, &t_gup->grp, lower, lea_cache_insert);
}

void lea_mapping_update(temp_map *map, blockmanager *bm, bool isgc){
    /*udpate mapping*/
    //figure out old mapping and invalidation itnnn
    if(!isgc){
        lea_mapping_find_exact_piece_ppa(map, true, bm);
    }
    else{
        //GDB_MAKE_BREAKPOINT;
    }

    if(map->size==0){
        printf("sorted buffer size is 0\n");
        abort();
    }


    /*do round*/
    uint32_t gup_idx=0;
    uint32_t start_ptr=UINT32_MAX;
    uint32_t transpage_idx=UINT32_MAX;
    uint32_t now_transpage_idx;
    std::list<group_update_param *> gup_list;
    group_update_param *t_gup;
    for(uint32_t i=0; i<map->size; i++){
        if(start_ptr==UINT32_MAX){
            start_ptr=i;
            transpage_idx=map->lba[i]/MAPINTRANS;
        }
        now_transpage_idx=map->lba[i]/MAPINTRANS;

        if(now_transpage_idx!=transpage_idx){
            t_gup=&gup_set[gup_idx++];
            lea_gup_init(t_gup, &main_gp[transpage_idx], map, start_ptr, i-start_ptr);
            gup_list.push_back(t_gup);

            start_ptr=i;
            transpage_idx=now_transpage_idx;
        }
    }
    t_gup=&gup_set[gup_idx++];
    lea_gup_init(t_gup, &main_gp[transpage_idx], map, start_ptr, map->size-start_ptr);
    gup_list.push_back(t_gup);

    std::list<group_update_param*>::iterator gup_iter;
    while(gup_list.size()){
        for(gup_iter=gup_list.begin(); gup_iter!=gup_list.end();){
            t_gup=*gup_iter;
            if(t_gup->gp->cache_flag==CACHE_FLAG::CACHED){
                lea_making_segment_per_gp(t_gup->gp, t_gup->map.lba, t_gup->map.piece_ppa, t_gup->map.interval, t_gup->map.size);
                gup_list.erase(gup_iter++);
            }
            else if(t_gup->gp->cache_flag==CACHE_FLAG::UNCACHED){
                printf("it must not be unCACHED\n");
                abort();
            }
            else{
                if(t_gup->grp.read_done){
                    /*header*/
                    group_read_from_NAND(t_gup->gp, false, &t_gup->grp, lower, lea_cache_insert);
                    lea_making_segment_per_gp(t_gup->gp, t_gup->map.lba, t_gup->map.piece_ppa, t_gup->map.interval, t_gup->map.size);
                    gup_list.erase(gup_iter++);
                    continue;
                }
                else{
                    gup_iter++;
                }
            }
        }
    }

#ifdef DEBUG
    //cutting mappint into segment set
    for (uint32_t i = 0; i < map->size; i++){
        exact_map[map->lba[i]]=map->piece_ppa[i];
    }
#endif

    /*temp_map clear*/
    temp_map_clear(map);
}

bool write_buffer_check_ignore(uint32_t lba){
    return wb->L2P_map.find(lba)!=wb->L2P_map.end();
}

uint32_t lea_write(request *const req){
    if(req->key==test_key){
        printf("%u is buffered\n", test_key);
    }
    lea_write_buffer_insert(wb, req->key, req->value->value);

    static int write_cnt=0;
    write_cnt++;
    if(write_cnt% COMPACTION_RESOLUTION ==0){
        for(uint32_t i=0; i<TRANSMAPNUM; i++){
            lea_compaction(i);
        }
        //group_monitor_print();
    }

    if(lea_write_buffer_isfull(wb)){
        uint32_t remain_space;
retry:
        remain_space=pm_remain_space(pm, true);

        if(lea_write_buffer_flush(wb, pm, user_temp_map, remain_space) == false){
            if(user_temp_map->size){
                lea_mapping_update(user_temp_map, pm->bm, false);
            }
            if(pm_assign_new_seg(pm) == false){
                /*gc_needed*/
                __gsegment *gc_target=pm_get_gc_target(pm->bm);
                if(pm->usedup_segments->find(gc_target->seg_idx) != pm->usedup_segments->end()){
                    pm_gc(pm, gc_target, gc_temp_map, true, write_buffer_check_ignore);
                    if(gc_temp_map->size){
                        lea_mapping_update(gc_temp_map, pm->bm, true);
                    }
                }
                else{
                    printf("usedup segment error!\n");
                    abort();
                }
            }
            goto retry;
        }
        else{
            lea_mapping_update(user_temp_map, pm->bm, false);
        }
        lea_write_buffer_clear(wb);
    }
    req->end_req(req);
    return 1;
}

uint32_t lea_remove(request *const req){
    return 1;
}