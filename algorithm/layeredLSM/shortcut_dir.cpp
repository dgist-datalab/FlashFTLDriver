#include "./shortcut_dir.h"
#include "./shortcut.h"
#include "../../include/debug_utils.h"
#ifdef SC_MEM_OPT
static inline void __dir_map_insert(shortcut_dir *dir, uint32_t index, uint32_t shortcut_idx){
    dir->table=(uint8_t*)realloc(dir->table, dir->map_num+1);
    if(dir->map_num!=0){
        uint32_t index_num=dir->map_num+1-index-1;
        memmove(&dir->table[index+1], &dir->table[index], index_num);
    }
    dir->table[index]=shortcut_idx;
    dir->map_num++;
}

static inline void __dir_map_update(shortcut_dir *dir, uint32_t index, uint32_t shortcut_idx){
    dir->table[index]=shortcut_idx;
}

static inline void __dir_map_erase(shortcut_dir *dir, uint32_t index){
    if(index!=dir->map_num-1){
        uint8_t last_shortcut=dir->table[dir->map_num-1];
        dir->table=(uint8_t*)realloc(dir->table, dir->map_num-1);
        dir->map_num--;

        uint32_t move_num=dir->map_num-index-1;
        memmove(&dir->table[index], &dir->table[index+1], move_num);
        dir->table[dir->map_num-1]=last_shortcut;
    }
    else{
        dir->table=(uint8_t*)realloc(dir->table, dir->map_num-1);
        dir->map_num--;
    }
}

void sc_dir_init(shortcut_dir *target,uint32_t idx, uint32_t init_value){
    target->bmap=bitmap_init(SC_PER_DIR);
    target->map_num=0;
    target->table=NULL;
    target->idx=idx;
    __dir_map_insert(target, 0, NOT_ASSIGNED_SC);
    bitmap_set(target->bmap, 0);
}

void sc_dir_free(shortcut_dir *target){
    bitmap_free(target->bmap);
    free(target->table);
}

static inline uint8_t __dir_map_query(shortcut_dir *dir, uint32_t index){
    return dir->table[index];
}

sc_dir_dp_master* dp_master;

static inline void compression_target(shortcut_dir *target, uint32_t target_idx, uint32_t offset, uint32_t sc_idx){
   uint32_t original_value=__dir_map_query(target, target_idx);
    //check next offset is set or not
    if (original_value != sc_idx){
        if (offset != SC_PER_DIR - 1){
            if (bitmap_is_set(target->bmap, offset + 1)){
                if(__dir_map_query(target, target_idx+1) == sc_idx){
                    //if the next entry is set and the table is the same sc_idx, the entry is changed to empty
                    bitmap_unset(target->bmap, offset+1);
                    __dir_map_erase(target, target_idx+1);
                }
            }
            else{
                //if the next entry has the same sc_idx with the current entry,
                //it needs to make the entry header
                bitmap_set(target->bmap, offset + 1);
                __dir_map_insert(target, target_idx+1, original_value);
            }
        }
        else{
            //don't care
        }

        //check previous entry
        if (target_idx != 0){
            if (__dir_map_query(target, target_idx - 1) == sc_idx){
                if (bitmap_is_set(target->bmap, offset)){
                    bitmap_unset(target->bmap, offset);
                    __dir_map_erase(target, target_idx);
                    goto end;
                }
            }
        }
    }
    else{
        goto end;
    }

    if(bitmap_is_set(target->bmap, offset)){
        __dir_map_update(target, target_idx, sc_idx);
    }
    else{
        bitmap_set(target->bmap, offset);
        __dir_map_insert(target, target_idx+1, sc_idx);
    }

end:
    return;
}
void sc_dir_insert_lba(shortcut_dir *target, uint32_t offset, uint32_t sc_idx){
    int32_t target_idx=-1;
    for(uint32_t i=0; i<=offset; i++){
        if(bitmap_is_set(target->bmap, i)){
            target_idx++;
        }
    }

    compression_target(target, target_idx, offset, sc_idx);
#if 0
    static uint32_t cnt=0;
    int32_t table_num=0;
    for(uint32_t i=0; i<SC_PER_DIR; i++){
        if(bitmap_is_set(target->bmap, i)){
            table_num++;
        }
    }
    cnt++;
    if(table_num!=target->map_num){
        EPRINT("%u error", true, cnt);
    }
#endif

    fdriver_lock(&dp_master->lock);
    std::multimap<uint32_t, sc_dir_dp *>::iterator iter = dp_master->dir_dp_tree->find(target->idx);
    for (; iter != dp_master->dir_dp_tree->end(); iter++)
    {   
        if(iter->second->prev_offset >= offset){
            iter->second->reinit=true;
        }
    }
    fdriver_unlock(&dp_master->lock);

    return;
}

void sc_dir_dp_master_init(){
    dp_master = (sc_dir_dp_master *)calloc(1, sizeof(sc_dir_dp_master));
    dp_master->dir_dp_tree = new std::multimap<uint32_t, sc_dir_dp *>();
    fdriver_mutex_init(&dp_master->lock);
}

void sc_dir_dp_master_free(){
    delete dp_master->dir_dp_tree;
    fdriver_destroy(&dp_master->lock);
    free(dp_master);
}

uint32_t sc_dir_query_lba(shortcut_dir *target, uint32_t offset){
    //list_iter target_iter=target->head_list->begin();
    int32_t target_idx=-1;
    for(uint32_t i=0; i<=offset; i++){
        if(bitmap_is_set(target->bmap, i)){
            target_idx++;
        }
    }
    return __dir_map_query(target, target_idx);
}
static void dp_reinit(sc_dir_dp *dp, sc_master *sc, uint32_t target_dir_idx){
    dp->target_dir=&sc->sc_dir[target_dir_idx];
    dp->prev_offset=0;
    dp->prev_table_idx=UINT32_MAX;
    dp->reinit=false;

    if(bitmap_is_set(dp->target_dir->bmap, 0)){
        dp->prev_table_idx++;
    }

    fdriver_lock(&dp_master->lock);
    std::multimap<uint32_t, sc_dir_dp*>::iterator iter=dp_master->dir_dp_tree->find(dp->prev_dir_idx);
    if(iter!=dp_master->dir_dp_tree->end()){
        for(;iter!=dp_master->dir_dp_tree->end(); iter++){
            if(iter->second==dp){
                dp_master->dir_dp_tree->erase(iter);
                break;
            }
        }
    }
    
    dp->prev_dir_idx=target_dir_idx;
    dp_master->dir_dp_tree->insert(std::pair<uint32_t, sc_dir_dp*>(dp->prev_dir_idx, dp));
    fdriver_unlock(&dp_master->lock);
}

sc_dir_dp *sc_dir_dp_init(sc_master *sc, uint32_t lba){
    sc_dir_dp *res=(sc_dir_dp*)calloc(1, sizeof(sc_dir_dp));

    dp_reinit(res, sc, lba/SC_PER_DIR);
    return res;
}

uint32_t sc_dir_dp_get_sc(sc_dir_dp *dp, sc_master *sc, uint32_t lba){
    uint32_t target_dir_idx=lba/SC_PER_DIR;

    if(dp->reinit || target_dir_idx!=dp->prev_dir_idx || dp->prev_offset > lba%SC_PER_DIR){
        dp_reinit(dp, sc, target_dir_idx);
    }
  
    if(dp->prev_dir_idx==target_dir_idx){
        int32_t target_offset=lba%SC_PER_DIR;
        for (int32_t i = dp->prev_offset+1; i <= target_offset; i++)
        {
            if (bitmap_is_set(dp->target_dir->bmap, i ))
            {
                dp->prev_table_idx++;
            }
        }
        dp->prev_offset=target_offset;
        return __dir_map_query(dp->target_dir, dp->prev_table_idx);
    }
    else{
        EPRINT("what happend?", false);
    }
    return UINT32_MAX;
}

uint32_t sc_dir_insert_lba_dp(shortcut_dir *target, sc_master *sc, uint32_t sc_idx, uint32_t start_idx, std::vector<uint32_t> *lba_set, bool unlink){
    uint32_t temp_offset=0;
    int target_table_idx=-1;
    uint32_t idx=start_idx;

    while(idx<lba_set->size() && (*lba_set)[idx]/SC_PER_DIR==target->idx){
        uint32_t target_offset=(*lba_set)[idx]%SC_PER_DIR;
        for (uint32_t i = temp_offset; i < target_offset; i++)
        {
            if (bitmap_is_set(target->bmap, i))
            {
                target_table_idx++;
            }
        }

        bool prev_check=bitmap_is_set(target->bmap, target_offset);
        if(prev_check){
            target_table_idx++; //the number of setted bit including itself
        }
		if(unlink){
			uint32_t info_idx=__dir_map_query(target, target_table_idx);
			if(info_idx!=NOT_ASSIGNED_SC){
				sc->info_set[info_idx].unlinked_lba_num++;
			}
		}
        compression_target(target, target_table_idx, target_offset, sc_idx);
        sc->info_set[sc_idx].linked_lba_num++;
        if(bitmap_is_set(target->bmap, target_offset)){
            if(!prev_check){
                target_table_idx++;
            }
        }
        else if(prev_check){
            target_table_idx--;
        }

        temp_offset=target_offset+1;
        idx++;
    }

    fdriver_lock(&dp_master->lock);
    std::multimap<uint32_t, sc_dir_dp *>::iterator iter = dp_master->dir_dp_tree->find(target->idx);
    for (; iter != dp_master->dir_dp_tree->end(); iter++)
    {   
        iter->second->reinit=true;
    }
    fdriver_unlock(&dp_master->lock);
    return idx;
}

void sc_dir_dp_free(sc_dir_dp* dp){
    fdriver_lock(&dp_master->lock);
    std::multimap<uint32_t, sc_dir_dp*>::iterator iter=dp_master->dir_dp_tree->find(dp->prev_dir_idx);
    for(;iter!=dp_master->dir_dp_tree->end(); iter++){
        if(iter->second==dp) break;
    }
    dp_master->dir_dp_tree->erase(iter);
    fdriver_unlock(&dp_master->lock);
    free(dp);
}
#endif
