#include "leaFTL.h"
#include "./write_buffer.h"
#include "./page_manager.h"
#include "./lea_container.h"
#include "./issue_io.h"
#include "../../include/debug_utils.h"
#include "group.h"
#include <vector>

#ifdef DEBUG
uint32_t *exact_map;
#endif

lea_write_buffer *wb;
page_manager *pm;
blockmanager *lea_bm;
lower_info *lower;
temp_map *user_temp_map;
temp_map *gc_temp_map;

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

    pm=pm_init(li, bm);

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
        grp->piece_ppa=group_get_retry(req->key, grp);
    }
    else{
        char *data = lea_write_buffer_get(wb, req->key);
        if (!data){
            grp=group_get(&main_gp[req->key/MAPINTRANS], req->key);
            if(grp==NULL){
                req->type=FS_NOTFOUND_T;
                return 0;
            }
            else{
                req->param=grp;
            }
            grp->value=req->value;
        }
        else{
            memcpy(req->value->value, data, LPAGESIZE);
            req->end_req(req);
            return 1;
        }
    }

    send_IO_user_req(DATAR, lower, grp->piece_ppa / L2PGAP, req->value, (void *)grp, req, lea_end_req);
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

void lea_mapping_update(temp_map *map, blockmanager *bm, bool isgc){
    /*udpate mapping*/
    //figure out old mapping and invalidation it
    if(!isgc){
        std::list<group_read_param*> grp_list;
        group_read_param *grp=(group_read_param*)malloc(sizeof(group_read_param)*map->size);
        for(uint32_t i=0; i<map->size; i++){
            if(map->lba[i]==test_key){
                //GDB_MAKE_BREAKPOINT;
            }
            group *gp=&main_gp[map->lba[i]/MAPINTRANS];
            group_get_exact_piece_ppa(gp, map->lba[i], i, &grp[i], true, lower);
            grp_list.push_back(&grp[i]);
        }

        while(grp_list.size()){
            std::list<group_read_param *>::iterator list_iter = grp_list.begin();
            for (; list_iter != grp_list.end();){
                group_read_param *t_grp=*list_iter;
                if(t_grp->lba==test_key){
                    //GDB_MAKE_BREAKPOINT;
                }
                if(t_grp->retry_flag==DATA_FOUND){
                    #ifdef DEBUG
                    if(t_grp->piece_ppa!=exact_map[t_grp->lba]){
                        printf("address error!\n");
                        //GDB_MAKE_BREAKPOINT;
                    }
                    #endif
                    if(t_grp->piece_ppa!=INITIAL_STATE_PADDR){
                        invalidate_piece_ppa(bm, t_grp->piece_ppa);
                    }
                    grp_list.erase(list_iter++);
                }
                else if(t_grp->read_done==false){
                    list_iter++;
                }
                else{
                    group_get_exact_piece_ppa(t_grp->gp, t_grp->lba, t_grp->set_idx, t_grp, false, lower);
                    list_iter++;
                }
            }
        }      
        free(grp);
    }
    else{
        //GDB_MAKE_BREAKPOINT;
    }

    if(map->size==0){
        printf("sorted buffer size is 0\n");
        abort();
    }

    uint32_t start_ptr=UINT32_MAX;
    uint32_t transpage_idx=UINT32_MAX;
    uint32_t now_transpage_idx;
    for(uint32_t i=0; i<map->size; i++){
        if(start_ptr==UINT32_MAX){
            start_ptr=i;
            transpage_idx=map->lba[i]/MAPINTRANS;
        }
        now_transpage_idx=map->lba[i]/MAPINTRANS;

        if(now_transpage_idx!=transpage_idx){
            lea_making_segment_per_gp(&main_gp[transpage_idx], &map->lba[start_ptr], &map->piece_ppa[start_ptr], &map->interval[start_ptr], i-start_ptr);
            start_ptr=i;
            transpage_idx=now_transpage_idx;
        }
    }
    lea_making_segment_per_gp(&main_gp[now_transpage_idx], &map->lba[start_ptr], &map->piece_ppa[start_ptr], &map->interval[start_ptr], map->size-start_ptr);

#ifdef DEBUG
    //cutting mappint into segment set
    for (uint32_t i = 0; i < map->size; i++){
        exact_map[map->lba[i]]=map->piece_ppa[i];
    }
#endif

    /*temp_map clear*/
    map->size = 0;
}

bool write_buffer_check_ignore(uint32_t lba){
    return wb->L2P_map.find(lba)!=wb->L2P_map.end();
}

uint32_t lea_write(request *const req){
    if(req->key==test_key){
        printf("%u is buffered\n", test_key);
    }
    lea_write_buffer_insert(wb, req->key, req->value->value);
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
                    lea_mapping_update(gc_temp_map, pm->bm, true);
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