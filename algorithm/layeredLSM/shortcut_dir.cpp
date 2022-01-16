#include "./shortcut_dir.h"
void sc_dir_init(shortcut_dir *target, uint32_t init_value){
    target->bmap=bitmap_init(SC_PER_DIR);
    target->head_list=new std::list<uint32_t>();
    target->head_list->push_front(init_value);
    bitmap_set(target->bmap,0);
}

void sc_dir_free(shortcut_dir *target){
    bitmap_free(target->bmap);
    delete target->head_list;
}
/*
static inline void __compressed_list_insert(compressed_list *compset, uint32_t idx, uint8_t sc){
    uint32_t chunk_idx=idx/(32/5);
    uint32_t chunk_offset=idx%(32/5);

    if(compset->allocated_num < chunk_idx+1){
        if(compset->list==NULL){
            compset->list=(uint32_t*)calloc(1, sizeof(uint32_t));
        }
        else{
            compset->list=(uint32_t*)realloc(compset->list, (chunk_idx+1)*sizeof(uint32_t));
        }
        compset->list[chunk_idx]=0;
        compset->allocated_num=chunk_idx+1;
    }
    compset->list[chunk_idx]|=(sc & 31) << (chunk_offset*5);
}

static inline uint32_t __compressed_list_query(compressed_list *compset, uint32_t idx){
    uint32_t chunk_idx=idx/(32/5);
    uint32_t chunk_offset=idx%(32/5);

    uint32_t test=0;
    test|=(31)<<(chunk_offset*5);

    test = compset->list[chunk_idx] & test;
    return test>>(chunk_offset*5);
}

static inline void __compressed_list_erase(compressed_list *list, uint32_t idx){
    uint32_t chunk_idx=idx/(32/5);
    uint32_t chunk_offset=idx%(32/5);

}*/

void sc_dir_insert_lba(shortcut_dir *target, uint32_t offset, uint32_t sc_idx){
    list_iter target_iter=target->head_list->begin();
    for(uint32_t i=1; i<=offset; i++){ //the firs entry always set
        if(bitmap_is_set(target->bmap, i)){
            target_iter++;
        }
    }

    uint32_t original_value=*target_iter;
    if(bitmap_is_set(target->bmap, offset)){
        /*head*/
        *target_iter=sc_idx;
        //check same sc_idx with previous sc_idx
        if(offset !=0){
            list_iter prev_target_iter=target_iter;
            --prev_target_iter;
            if(*prev_target_iter==*target_iter){
                bitmap_unset(target->bmap, offset);
                target->head_list->erase(target_iter--);
            }
        }
    }
    else{
        /*not head*/
        bitmap_set(target->bmap, offset);
        target->head_list->insert(++target_iter, sc_idx);
        target_iter--;
    }

    //check same sc_idx with next sc_idx
    if (offset != SC_PER_DIR - 1){
        if(bitmap_is_set(target->bmap, offset+1)){
            list_iter nxt_target_iter=target_iter;
            nxt_target_iter++;
            if(*nxt_target_iter==sc_idx){
                bitmap_unset(target->bmap, offset+1);
                target->head_list->erase(nxt_target_iter);
            }
        }
        else{
            bitmap_set(target->bmap, offset+1);
            target->head_list->insert(++target_iter,original_value);
        }
    }
}

uint32_t sc_dir_query_lba(shortcut_dir *target, uint32_t offset){
    list_iter target_iter=target->head_list->begin();
    for(uint32_t i=1; i<=offset; i++){
        if(bitmap_is_set(target->bmap, i)){
            target_iter++;
        }
    }
    return *target_iter;
}