#include "./group.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"
extern blockmanager *lea_bm;
extern uint32_t test_key;
bool leaFTL_debug;
void group_init(group *res, uint32_t idx){
    res->crb=crb_init();
    res->level_list=new std::vector<level*>();
    res->ppa=UINT32_MAX;
    res->map_idx=idx;
}

void print_all_level(std::vector<level*> *level_list){
    level_list_iter lev_list_iter=level_list->begin();
    for(;lev_list_iter!=level_list->end(); lev_list_iter++){
        level_iter lev_iter=(*lev_list_iter)->begin();
        for(;lev_iter!=(*lev_list_iter)->end(); lev_iter++){
            printf("%u,", lev_iter->first);
        }
        printf("\n");
    }
}

segment *group_get_segment(group *gp, uint32_t lba){
    if(lba==test_key){
        //GDB_MAKE_BREAKPOINT;
    }
    level_list_iter iter=gp->level_list->begin();
    CRB_node *app_target=crb_find_node(gp->crb, lba);
    for(uint32_t level_height=0; iter!=gp->level_list->end(); iter++, level_height++){
        level_iter lev_iter=(*iter)->begin();
        for(; lev_iter!=(*iter)->end(); lev_iter++){
            if(lev_iter->first > lba) break;
            if(lev_iter->second->end < lba) continue;
            if (lev_iter->second->type == SEGMENT_TYPE::ACCURATE){
                if(segment_acc_include(lev_iter->second, lba)){
                    return lev_iter->second;
                }
            }
            else{
                if(app_target && lev_iter->second == app_target->seg){
                    return lev_iter->second;
                }
            }
        }
    }
    return NULL;
}

static inline void grp_setup(group_read_param *grp, group *gp, segment *seg, uint32_t lba){
    grp->seg=seg;
    grp->oob=NULL;
    grp->gp=gp;
    grp->lba=lba;

    grp->piece_ppa=segment_get_addr(seg, lba);
    if(seg->type==SEGMENT_TYPE::ACCURATE){
        grp->retry_flag=DATA_FOUND;
        grp->read_done=true;
        grp->set_idx=grp->piece_ppa%L2PGAP;
    }
    else{
        grp->retry_flag=NOT_RETRY;
        grp->read_done=false;
        grp->set_idx=UINT32_MAX;
    }
    grp->value=NULL;
}

group_read_param *group_get(group *gp, uint32_t lba){
    segment *seg=group_get_segment(gp, lba);
    if(seg==NULL){
        return NULL;
    }
    else{
        group_read_param *res=(group_read_param*)malloc(sizeof(group_read_param));
        grp_setup(res, gp, seg, lba);
        return res;
    }
}

uint32_t group_oob_check(group_read_param *grp){
    for(uint32_t i=0; i<L2PGAP; i++){
        if(grp->oob[i]==grp->lba){
            return i;
        }
    }
    return NOT_FOUND;
}

uint32_t group_get_retry(uint32_t lba, group_read_param *grp){
    if(grp->retry_flag == NORMAL_RETRY){
        return NOT_FOUND;
    }
    if(lba < grp->oob[0]){
        grp->piece_ppa=(grp->piece_ppa/L2PGAP)*L2PGAP-1;
    }
    else if(lba > grp->oob[L2PGAP-1]){
        grp->piece_ppa=(grp->piece_ppa/L2PGAP+1)*L2PGAP;
    }
    grp->retry_flag=NORMAL_RETRY;
    return grp->piece_ppa;
}

void *group_param_free(group_read_param * param){
    free(param);
    return NULL;
}

static void find_overlap(level *lev, segment *seg, std::vector<segment*> *res){
    if(lev->size()==0) return;
    level_iter iter=lev->begin();
    for(; iter!=lev->end(); iter++){
        if(seg->end < iter->second->start || seg->start > iter->second->end){
            continue;
        }
        res->push_back(iter->second);
    }
}

static level* find_level_by_seg(group *gp, segment *seg){
    level_list_iter iter=gp->level_list->begin();

}

void group_update_segment(group *gp, std::vector<CRB_node>* arr){
    for(uint32_t i=0; i<arr->size(); i++){
        CRB_node target=arr->at(i);
        level *lev=find_level_by_seg(gp, target.seg);

    }
}

static void group_segment_update(group *gp, level *lev, temp_map *tmap, segment *new_seg, std::vector<segment*> *godown){
    std::vector<CRB_node> update_target_node;
    if(new_seg->type==SEGMENT_TYPE::APPROXIMATE){
        crb_insert(gp->crb, tmap, new_seg, &update_target_node);
    }
    else{
        crb_remove_overlap(gp->crb, tmap, &update_target_node);
    }

    if(update_target_node.size()){
        group_update_segment(gp, &update_target_node);
    }


    std::vector<segment*> overlapped;
    overlapped.clear();
    find_overlap(lev, new_seg, &overlapped);
    if(overlapped.size()){
        for (uint32_t i = 0; i < overlapped.size(); i++){
            segment *overlapped_seg = overlapped[i];
            bool remove_check = false;
            if (new_seg->type == SEGMENT_TYPE::ACCURATE){
                remove_check = segment_removable(overlapped_seg, new_seg);
            }
            else{
                remove_check = false;
                //not need to do
            }

            /*remove segment in this level*/
            level_iter iter = lev->find(overlapped_seg->start);
            lev->erase(iter);

            if (remove_check == false){
                godown->push_back(overlapped_seg);
            }
            else{
                // remove segment
                segment_free(overlapped_seg);
            }
        }
    }

    /*insert new_seg to this level*/
    std::pair<level_iter, bool> res=lev->insert(std::pair<uint32_t, segment*>(new_seg->start, new_seg));
    if(res.second == false){
        printf("overlapped found! %s:%u\n", __FUNCTION__, __LINE__);
        abort();
    }
}

void group_insert(group *gp, temp_map *tmap, SEGMENT_TYPE type, int32_t interval){
    /*
    static int cnt=0;
    printf("%s, %u, %lu, %u\n", __FUNCTION__, tmap->lba[0], tmap->lba[0]/MAPINTRANS, ++cnt);
    if(cnt==39087){
        //leaFTL_debug=true;
        //GDB_MAKE_BREAKPOINT;
    }*/

    segment *target=segment_make(tmap, type, interval);
    bool isfirst=false;
    if(gp->level_list->empty()){
        level *lev=new level();
        gp->level_list->push_back(lev);
        isfirst=true;
    }
    std::vector<segment*> godown;
    godown.clear();
    group_segment_update(gp, (*gp->level_list)[0], tmap, target, &godown);
    if(isfirst){
        gp->level_list->at(0)->insert(std::pair<uint32_t, segment*>(target->start, target));
    }
    else if(godown.size()){
        //insert new level 1;
        level *lev=new level();
        for(uint32_t i=0; i<godown.size(); i++){
            segment *seg=godown[i];
            lev->insert(std::pair<uint32_t, segment*>(seg->start, seg));
        }

        std::vector<level*>* new_level_list=new std::vector<level*>();
        new_level_list->reserve(gp->level_list->size()+1);
        new_level_list->push_back(gp->level_list->at(0));
        new_level_list->push_back(lev);

        for(uint32_t i=1; i<gp->level_list->size(); i++){
            new_level_list->push_back(gp->level_list->at(i));
        }

        delete gp->level_list;
        gp->level_list=new_level_list;
    }
}

void group_compaction(group *gp){

}

void group_to_translation_map(group *gp, char *des){
    group_compaction(gp);
}

void group_free(group *gp){
    crb_free(gp->crb);
    level_list_iter iter=gp->level_list->begin();
    for(;iter!=gp->level_list->end(); iter++){
        level_iter lev_iter=(*iter)->begin();
        for(;lev_iter!=(*iter)->end(); lev_iter++){
            segment_free(lev_iter->second);
        }
        delete (*iter);
    }
    delete gp->level_list;
    free(gp);
}

void *group_read_end_req(algo_req *algo){
    group_read_param *grp=(group_read_param*)algo->param;
    grp->read_done=true;
    grp->oob=(uint32_t*)lea_bm->get_oob(lea_bm, grp->piece_ppa/L2PGAP);
    grp->set_idx=group_oob_check(grp);
    if(grp->set_idx==NOT_FOUND){
        //should be requery
    }
    else{
        grp->piece_ppa=grp->piece_ppa/L2PGAP*L2PGAP+grp->set_idx;
        inf_free_valueset(grp->value, FS_MALLOC_R);
        grp->retry_flag=DATA_FOUND;
    }
    free(algo);
    return NULL;
}

void group_get_exact_piece_ppa(group *gp, uint32_t lba, uint32_t set_idx, group_read_param *grp, bool isstart, lower_info *li){
    if(lba==test_key){
        //GDB_MAKE_BREAKPOINT;
        //print_all_level(gp->level_list);
    }
    if(isstart){
        grp->gp=gp;
        grp->lba=lba;
        grp->seg=group_get_segment(gp, lba);
        grp->value=NULL;
        if(grp->seg==NULL){ //not inserted
            grp->piece_ppa=INITIAL_STATE_PADDR;
            grp->read_done=true;
            grp->retry_flag=DATA_FOUND;
        }
        else{
            if(grp->seg->start==lba){
                grp->piece_ppa=grp->seg->start_piece_ppa;
                grp->read_done=true;
                grp->retry_flag=DATA_FOUND;
            }
            else{
                grp_setup(grp, gp, grp->seg, lba);
                grp->piece_ppa = segment_get_addr(grp->seg, lba);
                if (grp->seg->type == SEGMENT_TYPE::APPROXIMATE){
                    grp->value = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
                    // issue request
                    send_IO_back_req(DATAR, li, grp->piece_ppa / L2PGAP, grp->value, (void *)grp, group_read_end_req);
                }
            }
        }
    }
    else{
        grp->piece_ppa=group_get_retry(lba, grp);
        if(grp->piece_ppa==NOT_FOUND){
            inf_free_valueset(grp->value, FS_MALLOC_R);
            grp->read_done=true;
            grp->retry_flag=DATA_FOUND;
        }
        else{
            grp->read_done=false;
            //issue request
            send_IO_back_req(DATAR, li, grp->piece_ppa/L2PGAP, grp->value, (void*)grp, group_read_end_req);
        }
    }
}