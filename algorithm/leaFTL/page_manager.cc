#include "./page_manager.h"
#include "./issue_io.h"
#include "../../interface/interface.h"
#include "../../include/data_struct/list.h"
#include "../../include/debug_utils.h"
#include <string.h>
#include <algorithm>
extern uint32_t test_key;
uint32_t lea_test_piece_ppa=UINT32_MAX;
enum SEGTYPE_FLAG{
    NONE, DATA, TRANS
};
SEGTYPE_FLAG seg_type_flag[_NOS];
#define TRANSLATION_MAP_FLAG (UINT32_MAX-1)
typedef struct io_param{
    uint32_t type;
    uint32_t ppa;
    volatile bool isdone;
    value_set *value;
}io_param;


typedef struct gc_node{
    uint32_t lba;
    char *value;
}gc_node;

bool gc_node_cmp(gc_node a, gc_node b){
    return a.lba < b.lba;
}

void g_buffer_init(align_buffer *g_buffer){
    g_buffer->value=(char**)malloc(sizeof(char*)*L2PGAP);
    for(uint32_t i=0; i<L2PGAP; i++){
        g_buffer->value[i]=(char*)malloc(LPAGESIZE);
    }
    g_buffer->idx=0;
}

void g_buffer_free(align_buffer *g_buffer){
    for(uint32_t i=0; i<L2PGAP; i++){
        free(g_buffer->value[i]);
    }
    free(g_buffer->value);
}

void g_buffer_to_temp_map(align_buffer *g_buffer, temp_map *res, uint32_t *piece_ppa_arr){
    for(uint32_t idx=0; idx<g_buffer->idx; idx++){
        res->lba[res->size]=g_buffer->key[idx];
        res->piece_ppa[res->size]=piece_ppa_arr[idx];
        if(res->size!=0){
            res->interval[res->size-1]=res->lba[res->size]-res->lba[res->size-1];
            if(res->interval[res->size-1] < 0){
                printf("interval less than 0, not sorted set!\n");
                abort();
            }
        }
        res->size++;
    }
    g_buffer->idx=0;
}

void g_buffer_insert(align_buffer *g_buffer, char *data, uint32_t lba){
    memcpy(g_buffer->value[g_buffer->idx], data, LPAGESIZE);
    g_buffer->key[g_buffer->idx]=lba;
    g_buffer->idx++;
}

void invalidate_piece_ppa(blockmanager *bm, uint32_t piece_ppa){
    bm->bit_unset(bm, piece_ppa);
    if(piece_ppa==lea_test_piece_ppa){
        printf("%u is invalidate\n", piece_ppa);
    }
}

static void validate_ppa(blockmanager *bm, uint32_t ppa, KEYT *lba, uint32_t max_idx){
    for(uint32_t i=0; i<max_idx; i++){
        bm->bit_set(bm, ppa*L2PGAP+i);
        if(lba[i]==test_key){
            printf("%u is mapped to %u\n", lba[i], ppa*L2PGAP+i);
        }
    }
    bm->set_oob(bm, (char*)lba, sizeof(uint32_t) * max_idx, ppa);
}

page_manager *pm_init(lower_info *li, blockmanager *bm){
    page_manager *res=(page_manager*)malloc(sizeof(page_manager));
    res->lower=li;
    res->bm=bm;

    res->reserve=res->bm->get_segment(res->bm, BLOCK_RESERVE);
    res->active=res->bm->get_segment(res->bm, BLOCK_ACTIVE);
    return res;
}

bool pm_assign_new_seg(page_manager *pm, bool isdata){
    blockmanager *bm = pm->bm;
    seg_type_flag[pm->active->seg_idx]=isdata?SEGTYPE_FLAG::DATA:SEGTYPE_FLAG::TRANS;

    if(bm->is_gc_needed(bm)){
        return false;
    }
    
    pm->active = bm->get_segment(bm, BLOCK_ACTIVE);
    if(seg_type_flag[pm->active->seg_idx]!=SEGTYPE_FLAG::NONE){
        printf("not gced segment assign?!\n");
        GDB_MAKE_BREAKPOINT;
    }

    if (pm_remain_space(pm, true) == 0){
        printf("error on getting new active block, already full!\n");
        abort();
    }
    return true;
}

static inline ppa_t get_ppa(page_manager *pm, blockmanager *bm, __segment *seg){
    if(bm->check_full(seg)){
        printf("already full segment %s:%u\n", __FUNCTION__, __LINE__);
        abort();
        return UINT32_MAX;
    }
    ppa_t res=bm->get_page_addr(seg);
    return res;
}

void *pm_end_req(algo_req * const al_req){
    io_param *params=(io_param*)al_req->param;
    switch(params->type){
        case MAPPINGW:
        case GCMW:
        case GCDW:
        case DATAW:
            inf_free_valueset(params->value, FS_MALLOC_W);
            free(params);
            break;
        case GCMR:
        case GCDR:
            params->isdone=true;
            break;
    }
    free(al_req);
    return NULL;
}

static inline io_param *make_io_param(uint32_t type, uint32_t ppa, value_set *value){
    io_param *res=(io_param*)malloc(sizeof(io_param));
    res->type=type;
    res->ppa=ppa;
    res->isdone=false;
    res->value=value;
    return res;
}

void pm_page_flush(page_manager *pm, bool isactive, uint32_t type, uint32_t *lba, char **data, uint32_t size, uint32_t *piece_ppa_res){
    ppa_t ppa=get_ppa(pm, pm->bm, isactive? pm->active: pm->reserve);
    if(ppa==UINT32_MAX){
        printf("you should call gc before flushing data!\n");
        abort();
    }
    
    value_set *value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
    for(uint32_t i=0; i<size; i++){
        memcpy(&value->value[i*LPAGESIZE], data[i], LPAGESIZE);
        piece_ppa_res[i]=ppa*L2PGAP+i;
    }
    io_param *params=make_io_param(DATAW, ppa, value);

    send_IO_back_req(type, pm->lower, ppa, value, (void*)params, pm_end_req);
    validate_ppa(pm->bm, ppa, lba, size);
}

uint32_t pm_remain_space(page_manager *pm, bool isactive){
    if(pm->bm->check_full(isactive?pm->active:pm->reserve)){
        return 0;
    }
    else{
        return _PPS-(pm->bm->pick_page_addr(isactive?pm->active:pm->reserve)%_PPS);
    }
}

__gsegment* pm_get_gc_target(blockmanager *bm){
    return bm->get_gc_target(bm);
}

static io_param *send_gc_req(lower_info *lower, uint32_t ppa, uint32_t type, value_set *value){
    io_param *res=NULL;
    value_set *target_value;
    switch(type){
        case GCMR:
        target_value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
        res=make_io_param(type, ppa, target_value);
        break;
        case GCDR:
        target_value=inf_get_valueset(NULL,FS_MALLOC_R, PAGESIZE);
        res=make_io_param(type, ppa, target_value);
        break;
    }
    send_IO_back_req(type, lower, ppa, target_value, res, pm_end_req);
    return res;
} 


static inline void pm_gc_finish(page_manager *pm, blockmanager *bm, __gsegment *target){
    seg_type_flag[target->seg_idx]=SEGTYPE_FLAG::NONE;

    if(target->seg_idx==lea_test_piece_ppa/4/_PPS){
        printf("%u clear!\n", lea_test_piece_ppa);
    }

    /*reset active segment and reserve segment*/
    bm->trim_segment(bm ,target);
    pm->active=pm->reserve;
    bm->change_reserve_to_active(bm, pm->reserve);
    pm->reserve=bm->get_segment(bm, BLOCK_RESERVE);
    if(pm->reserve->invalidate_piece_num!=0){
        abort();
    }
}

void pm_data_gc(page_manager *pm, __gsegment *target, temp_map *res){
    uint32_t page;
    uint32_t bidx, pidx;
    blockmanager* bm=pm->bm;
    list *temp_list=list_init();
    io_param *gp; //gc_param;

    /*read phase*/
    for_each_page_in_seg(target, page, bidx, pidx){
        bool should_read=false;
        for(uint32_t i=0; i<L2PGAP; i++){
            if(bm->is_invalid_piece(bm, page*L2PGAP+i)) continue;
            else{
                should_read=true;
                break;
            }
        }
        if(should_read){
            gp=send_gc_req(pm->lower, page, GCDR, NULL);
            list_insert(temp_list, (void*)gp);
        }
    }

    /*converting read data*/
    li_node *now, *nxt;
    uint32_t* lba_arr;
    std::vector<gc_node> temp_vector;
    gc_node gn;
    for_each_list_node_safe(temp_list, now, nxt){
        gp = (io_param *)now->data;
        while(gp->isdone==false){}
        lba_arr = (uint32_t *)bm->get_oob(bm, gp->ppa);
        for (uint32_t i = 0; i < L2PGAP; i++){
            if (bm->is_invalid_piece(bm, gp->ppa * L2PGAP + i)){
                continue;
            }
            gn.lba = lba_arr[i];
            gn.value = &gp->value->value[i * LPAGESIZE];
            temp_vector.push_back(gn);
        }
    }

    sort(temp_vector.begin(), temp_vector.end(), gc_node_cmp);

    /*write_data*/
    align_buffer g_buffer;
    g_buffer_init(&g_buffer);
    uint32_t piece_ppa_arr[L2PGAP];
    for(uint32_t i=0; i<temp_vector.size(); i++){
        if(temp_vector[i].lba==test_key){
            //GDB_MAKE_BREAKPOINT;
        }
        g_buffer_insert(&g_buffer, temp_vector[i].value, temp_vector[i].lba);
        if (g_buffer.idx == L2PGAP || (i==temp_vector.size()-1 && g_buffer.idx!=0)){
            pm_page_flush(pm, false, GCDW, g_buffer.key, g_buffer.value, g_buffer.idx, piece_ppa_arr);
            g_buffer_to_temp_map(&g_buffer, res, piece_ppa_arr);
        }
    }
    g_buffer_free(&g_buffer);

    /*clean memory*/
    for_each_list_node_safe(temp_list, now, nxt){
        gp=(io_param*)now->data;
        inf_free_valueset(gp->value, FS_MALLOC_R);
        free(gp);
        list_delete_node(temp_list, now);
    }
    list_free(temp_list);

    pm_gc_finish(pm, bm, target);
    return;
}

uint32_t get_map_ppa(page_manager *pm, bool isactive, uint32_t gp_idx){
    ppa_t ppa=get_ppa(pm, pm->bm, isactive?pm->active: pm->reserve);
    if(ppa==UINT32_MAX){
        printf("you should call gc before flushing data!\n");
        abort();
    }

    uint32_t oob[L2PGAP];
    oob[0]=gp_idx;
    oob[1]=TRANSLATION_MAP_FLAG;
    validate_ppa(pm->bm, ppa, oob, L2PGAP);
    return ppa;
}

void pm_map_gc(page_manager *pm, __gsegment *target, temp_map *res){
    uint32_t page;
    uint32_t bidx, pidx;
    blockmanager* bm=pm->bm;
    list *temp_list=list_init();
    io_param *gp; //gc_param;

    /*read phase*/
    for_each_page_in_seg(target, page, bidx, pidx){
        if(bm->is_invalid_piece(bm, page*L2PGAP)){
            continue;
        }
        gp=send_gc_req(pm->lower, page, GCMR, NULL);
        list_insert(temp_list, (void*)gp);
    }

    li_node *now, *nxt;
    uint32_t* lba_arr;
    std::vector<gc_node> temp_vector;
    for_each_list_node_safe(temp_list, now, nxt){
        gp = (io_param *)now->data;
        while(gp->isdone==false){}
        lba_arr = (uint32_t *)bm->get_oob(bm, gp->ppa);
        if(lba_arr[1]!=TRANSLATION_MAP_FLAG){
            printf("it is not translation page!\n");
            GDB_MAKE_BREAKPOINT;
        }

        uint32_t new_ppa=get_map_ppa(pm, false, lba_arr[0]);
        res->lba[res->size]=lba_arr[0];
        res->piece_ppa[res->size]=new_ppa;
        send_IO_back_req(GCMW, pm->lower, new_ppa, gp->value, (void*)gp, pm_end_req);
        res->size++; 
    }
    pm_gc_finish(pm, pm->bm, target);
}

void temp_queue_to_heap(blockmanager *bm, std::queue<uint32_t> *temp_queue){

}

void pm_gc(page_manager *pm, temp_map *res, bool isdata){
    __gsegment *gc_target=NULL;

    std::queue<uint32_t> temp_queue;
    while(gc_target!=NULL){
        gc_target=pm_get_gc_target(pm->bm);
        if (gc_target->all_invalid){
            pm_gc_finish(pm, pm->bm, gc_target);
            temp_map_clear(res);
            temp_queue_to_heap(pm->bm, &temp_queue);
            return;
        }

        if(isdata){
            if(seg_type_flag[gc_target->seg_idx]==SEGTYPE_FLAG::DATA){
                break;
            }
        }
        else{
            if(seg_type_flag[gc_target->seg_idx]==SEGTYPE_FLAG::TRANS){
                break;
            }
        }
        temp_queue.push(gc_target->seg_idx);
        free(gc_target);
    }
    temp_queue_to_heap(pm->bm, &temp_queue);
    
    if(isdata){
        pm_data_gc(pm, gc_target, res);
    }
    else{
        pm_map_gc(pm, gc_target, res);
    }
}

uint32_t pm_map_flush(page_manager *pm, bool isactive, char *data, uint32_t gp_idx){
    uint32_t ppa=get_map_ppa(pm, isactive, gp_idx);
    value_set *value=inf_get_valueset(data, FS_MALLOC_W, PAGESIZE);
    io_param *params=make_io_param(MAPPINGW, ppa, value);
    send_IO_back_req(MAPPINGW, pm->lower, ppa, value, (void*)params, pm_end_req);
    return ppa;
}

void pm_free(page_manager *pm){
    free(pm);
}