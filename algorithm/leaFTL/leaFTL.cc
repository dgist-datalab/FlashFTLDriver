#include "leaFTL.h"
#include "./write_buffer.h"
#include "./page_manager.h"
#include "./lea_container.h"
#include "./issue_io.h"
#include "../../include/debug_utils.h"

uint32_t *exact_map;
lea_write_buffer *wb;
page_manager *pm;
lower_info *lower;
temp_map user_temp_map;
temp_map gc_temp_map;
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

    exact_map=(uint32_t*)malloc(sizeof(uint32_t) * _NOP * L2PGAP);
    memset(exact_map, -1, sizeof(uint32_t)*_NOP*L2PGAP);

    wb=lea_write_buffer_init(WRITE_BUF_SIZE);
    uint32_t sector_num=WRITE_BUF_SIZE/L2PGAP;
    user_temp_map.lba=(uint32_t *)malloc(sizeof(uint32_t)*sector_num);
    user_temp_map.piece_ppa=(uint32_t *)malloc(sizeof(uint32_t)*sector_num);

    sector_num=_PPS*L2PGAP;
    gc_temp_map.lba=(uint32_t *)malloc(sizeof(uint32_t)*sector_num);
    gc_temp_map.piece_ppa=(uint32_t *)malloc(sizeof(uint32_t)*sector_num);
    pm=pm_init(li, bm);
    return 1;
}

void lea_destroy(lower_info *, algorithm *){
    lea_write_buffer_free(wb);
    pm_free(pm);
    free(exact_map);
    free(user_temp_map.lba);
    free(user_temp_map.piece_ppa);
}

uint32_t lea_argument(int argc, char **argv){
    return 1;
}

void *lea_end_req(algo_req* const req){
    user_io_param *param=(user_io_param*)req->param;
    uint32_t idx;
    switch(param->type){
        case DATAR:
        idx=param->piece_ppa%L2PGAP;
        if(idx){
            memmove(param->value->value, &param->value->value[(idx)*LPAGESIZE], LPAGESIZE);
        }
        break;
    }

    if(req->parents){
        req->parents->end_req(req->parents);
    }

    free(param);
    free(req);
    return NULL;
}

uint32_t lea_read(request *const req){
    char *data=lea_write_buffer_get(wb,req->key);
    if(!data){
        uint32_t piece_ppa=exact_map[req->key];
        user_io_param *param=get_user_io_param(DATAR, piece_ppa, req->value);
        send_IO_user_req(DATAR, lower, piece_ppa/L2PGAP, req->value, (void*)param, req, lea_end_req);
    }
    else{
        memcpy(req->value->value,data, LPAGESIZE);
        req->end_req(req);
    }
    return 1;
}

void lea_mapping_update(temp_map *map, blockmanager *bm, bool isgc){
    /*udpate mapping*/
    for (uint32_t i = 0; i < map->size; i++){
        if(exact_map[map->lba[i]]!=INITIAL_STATE_PADDR && isgc==false){
            invalidate_piece_ppa(bm, exact_map[map->lba[i]]);
        }
        exact_map[map->lba[i]]=map->piece_ppa[i];
    }
    /*temp_map clear*/
    map->size = 0;
}

bool write_buffer_check_ignore(uint32_t lba){
    return wb->L2P_map.find(lba)!=wb->L2P_map.end();
}

uint32_t lea_write(request *const req){
    lea_write_buffer_insert(wb, req->key, req->value->value);
    if(lea_write_buffer_isfull(wb)){
        uint32_t remain_space;
retry:
        remain_space=pm_remain_space(pm, true);
        if(lea_write_buffer_flush(wb, pm, &user_temp_map, remain_space) == false){
            lea_mapping_update(&user_temp_map, pm->bm, false);
            if(pm_assign_new_seg(pm) == false){
                /*gc_needed*/
                __gsegment *gc_target=pm_get_gc_target(pm->bm);
                if(pm->usedup_segments->find(gc_target->seg_idx) != pm->usedup_segments->end()){
                    pm_gc(pm, gc_target, &gc_temp_map, true, write_buffer_check_ignore);
                    lea_mapping_update(&gc_temp_map, pm->bm, true);
                }
                else{
                    printf("usedup segment error!\n");
                    abort();
                }
            }
            goto retry;
        }
        else{
            lea_mapping_update(&user_temp_map, pm->bm, false);
        }
        lea_write_buffer_clear(wb);
    }
    req->end_req(req);
    return 1;
}

uint32_t lea_remove(request *const req){
    return 1;
}