#include "./group.h"
#include "../../include/data_struct/bitmap.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include <algorithm>
extern blockmanager *lea_bm;
extern uint32_t test_key;
//segment *debug_segment;
bool leaFTL_debug;
group_monitor gm;
void group_init(group *res, uint32_t idx){
    res->crb=crb_init();
    res->level_list=new std::vector<level*>();
    res->ppa=UINT32_MAX;
    res->map_idx=idx;
    res->cache_flag=CACHE_FLAG::UNCACHED;
    res->size=0;
    res->lru_node=NULL;
}

void print_all_level(std::vector<level*> *level_list){
    level_list_iter lev_list_iter=level_list->begin();
    for(;lev_list_iter!=level_list->end(); lev_list_iter++){
        level_iter lev_iter=(*lev_list_iter)->begin();
        for(;lev_iter!=(*lev_list_iter)->end(); lev_iter++){
            printf("%u,", (*lev_iter)->start);
        }
        printf("\n");
    }
}

bool seg_compare(segment *a, segment *b){
    return a->start < b->start;
}

void check_level_overlap(level *lev){
    if(lev->size() < 2)  return;
    level_iter iter, prev_iter;
    for(prev_iter=lev->begin(), iter=lev->begin()+1; iter!=lev->end(); iter++, prev_iter++){
        if((*iter)->start > (*prev_iter)->end){
            continue;
        }
        else{
            printf("level constraint error!\n");
            abort();
        }
    }
}

uint64_t group_level_size(level *lev){
    uint64_t res=0;
    level_iter iter;
    for(iter=lev->begin()+1; iter!=lev->end(); iter++){
        res+=segment_size(*iter);
    }
    return res;
}

level_iter level_lower_bound_wrapper(level *lev, segment *seg, bool *found){
    if(lev->size()==0){
        *found=false;
        return lev->end();
    }
    level_iter lev_iter=std::lower_bound(lev->begin(), lev->end(), seg, seg_compare);
    if(lev_iter==lev->end()){
        /*overlap check on the last element*/
        if(lev->back()->start > seg->end || lev->back()->end < seg->start){
            *found=false;
            return lev_iter;
        }
        else{
            *found=true;
            return lev->end()-1;
        }
    }
    else if((*lev_iter)->start > seg->start && lev_iter!=lev->begin()){
        lev_iter--;
    }
    *found=true;

    return lev_iter;
}

segment *group_get_segment(group *gp, uint32_t lba){
    if(lba==test_key){
        //GDB_MAKE_BREAKPOINT;
    }
    level_list_iter iter=gp->level_list->begin();
    CRB_node *app_target=crb_find_node(gp->crb, lba);
    segment temp;
    temp.start=lba;
    temp.end=lba;
    bool found;
    for(uint32_t level_height=0; iter!=gp->level_list->end(); iter++, level_height++){

        if(lba==test_key && level_height==11){
            //GDB_MAKE_BREAKPOINT;
        }
        level_iter lev_iter=level_lower_bound_wrapper((*iter), &temp, &found);
        if(!found) {
            continue;
        }
        if(!((*lev_iter)->start <= lba && lba<=(*lev_iter)->end)){ //filtering
            continue;
        }

        segment *target = *lev_iter;
        if (target->type == SEGMENT_TYPE::ACCURATE){
            if (segment_acc_include(target, lba)){
                return target;
            }
        }
        else{
            if (app_target && target == app_target->seg){
                return target;
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

group_read_param *group_get(group *gp, uint32_t lba, request *req){
    segment *seg=group_get_segment(gp, lba);
    if(seg==NULL){
        return NULL;
    }
    else{
        group_read_param *res=(group_read_param*)malloc(sizeof(group_read_param));
        grp_setup(res, gp, seg, lba);
        res->value=req->value;
        req->param=res;
        send_IO_user_req(DATAR, lower, grp->piece_ppa / L2PGAP, req->value, (void *)grp, req, lea_end_req);
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

uint32_t group_get_retry(uint32_t lba, group_read_param *grp, request *req, void (*cache_insert)(group *gp, uint32_t *piece_ppa)){
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
    bool found;
    level_iter iter=level_lower_bound_wrapper(lev, seg, &found);
    if(found==false) return;
    
    for(; iter!=lev->end(); iter++){
        /*if not overlap, continue*/
        if((*iter)->start > seg->end || (*iter)->end < seg->start){
            continue;
        }
        res->push_back(*iter);
    }
}

static level* find_level_by_seg(group *gp, segment *seg, uint32_t *res_idx){
    level_list_iter iter=gp->level_list->begin();
    bool found;
    for(; iter!=gp->level_list->end(); iter++){
        level_iter target_iter=level_lower_bound_wrapper(*iter, seg, &found);
        if(!found) continue;
        uint32_t idx=std::distance((*iter)->begin(), target_iter);
        if(*target_iter==seg){
            *res_idx=idx;
            return *iter;
        }
    }
    printf("we must find the proper entry!\n");
    abort();
    return NULL;
}

void group_update_segment(group *gp, std::vector<CRB_node>* arr){
    for(uint32_t i=0; i<arr->size(); i++){
        CRB_node target=arr->at(i);
        uint32_t idx;
        level *lev=find_level_by_seg(gp, target.seg, &idx);
        if(target.lba_arr){
            segment_update(lev->at(idx), target.lba_arr->at(0), target.lba_arr->back());
        }
        else{
            //remove entry;
            lev->erase(lev->begin()+idx);
        }
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
    bool found;
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
            //level_iter iter = std::lower_bound(lev->begin(), lev->end(), overlapped_seg, seg_compare);
            level_iter iter = level_lower_bound_wrapper(lev, overlapped_seg, &found);
            if((*iter) == overlapped_seg){
                lev->erase(iter);
            }
            else{
                printf("the segment must be found!\n");
                abort();
            }

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
    level_iter iter=level_lower_bound_wrapper(lev, new_seg, &found);
    if(!found){ // insert to the last member
        iter=lev->begin()+lev->size();
    }
    else{
        if((*iter)->start < new_seg->start){
            iter++;
        }
    }
    lev->insert(iter, new_seg);
#ifdef DEBUG
    check_level_overlap(lev);
#endif
}

void group_insert(group *gp, temp_map *tmap, SEGMENT_TYPE type, int32_t interval){
    /*
    static int cnt=0;
    printf("%s, %u, %lu, %u\n", __FUNCTION__, tmap->lba[0], tmap->lba[0]/MAPINTRANS, ++cnt);
    if(cnt==277397){
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
    gp->size-=group_level_size(gp->level_list->at(0));
    group_segment_update(gp, (*gp->level_list)[0], tmap, target, &godown);
    gp->size+=group_level_size(gp->level_list->at(0));
    if(godown.size()){
        //insert new level 1;
        uint64_t new_level_size=0;
        level *lev=new level();
        for(uint32_t i=0; i<godown.size(); i++){
            segment *seg=godown[i];
            new_level_size+=segment_size(seg);
            lev->push_back(seg);
        }
        gp->size+=new_level_size;


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

#ifdef DEBUG
    level_list_iter iter=gp->level_list->begin();
    for(iter; iter!=gp->level_list->end(); iter++){
        check_level_overlap(*iter);
    }
#endif
}

segment *map_make_segment_wrapper(uint32_t *lba, uint32_t *piece_ppa, uint32_t size){
    temp_map param_map;
    param_map.lba=lba;
    param_map.piece_ppa=piece_ppa;
    param_map.size=size;

    if(size==1){
        return segment_make(&param_map, SEGMENT_TYPE::ACCURATE, 0);
    }
    else{
        return segment_make(&param_map, SEGMENT_TYPE::ACCURATE, 1);
    }
}


uint32_t temp_lba[MAPINTRANS];
level* map_to_onelevel(group *gp, uint32_t *t_lba, uint32_t *piece_ppa){
    if(gp->map_idx==test_key/MAPINTRANS){
        printf("target %u compacted\n", test_key);
    }
    uint32_t *lba;
    if(t_lba==NULL){
        for(uint32_t i=0; i<MAPINTRANS; i++){
            temp_lba[i]=gp->map_idx*MAPINTRANS+i;
        }
        lba=temp_lba;
    }
    else{
        lba=t_lba;
    }
    

    level* res=new level();
    uint32_t prev_piece_ppa=piece_ppa[0];
    uint32_t start_idx=0;
    uint32_t target_size=1;
    bool have_remain=false;
    for(uint32_t i=1; i<MAPINTRANS; i++){
        if(lba[i]==test_key){
            //GDB_MAKE_BREAKPOINT;
        }
        if(prev_piece_ppa==INITIAL_STATE_PADDR){
            //start_from now
            start_idx=i;
            target_size=1;
            have_remain=true;
        }
        else if(piece_ppa[i]-prev_piece_ppa==1){
            target_size++;
            have_remain=true;
        }
        else{
            segment *target=map_make_segment_wrapper(&lba[start_idx], &piece_ppa[start_idx], target_size);
            res->push_back(target);

            if(piece_ppa[i]==INITIAL_STATE_PADDR){
                have_remain=false;
            }
            else{
                start_idx=i;
                target_size=1;
                have_remain=true;
            }
        }
        prev_piece_ppa=piece_ppa[i];
    }
    
    if(have_remain){
        segment *target=map_make_segment_wrapper(&lba[start_idx], &piece_ppa[start_idx], target_size);
        res->push_back(target);
    }

#ifdef DEBUG
    /*
    level_iter iter=res->begin();
    for(iter; iter!=res->end(); iter++){
        segment_print(*iter);
    }*/
    check_level_overlap(res);
#endif
    return res;
}

void group_from_translation_map(group *gp, uint32_t *lba, uint32_t *piece_ppa, uint32_t idx){
    group_clean(gp, true);
    level *new_level=map_to_onelevel(gp, lba, piece_ppa);
    gp->size=group_level_size(new_level);
    gp->level_list->push_back(new_level);
}

void group_clean(group *gp, bool reinit){
    uint32_t segment_num=0;
    level_list_iter iter=gp->level_list->begin();
    for(;iter!=gp->level_list->end(); iter++){
        level_iter lev_iter=(*iter)->begin();
        for(;lev_iter!=(*iter)->end(); lev_iter++){
            segment_free(*lev_iter);
            segment_num++;
        }
        delete (*iter);
    }
    if(reinit){
        gp->level_list->clear();
        crb_free(gp->crb);
        gp->crb=crb_init();
    }
    else{
        crb_free(gp->crb);
        delete gp->level_list;
    }
    gp->size=0;

    gm.total_segment+=segment_num;
    gm.interval_segment=segment_num;
}

void group_free(group *gp){
    group_clean(gp, false);
}

void *group_read_end_req(algo_req *algo){
    group_read_param *grp=(group_read_param*)algo->param;
    if(algo->type==MAPPINGR){
        grp->read_done=true;
    }
    else if(algo->type==DATAR){
        grp->read_done = true;
        grp->oob = (uint32_t *)lea_bm->get_oob(lea_bm, grp->piece_ppa / L2PGAP);
        grp->set_idx = group_oob_check(grp);
        if (grp->set_idx == NOT_FOUND){
            // should be requery
        }
        else{
            grp->piece_ppa = grp->piece_ppa / L2PGAP * L2PGAP + grp->set_idx;
            inf_free_valueset(grp->value, FS_MALLOC_R);
            grp->retry_flag = DATA_FOUND;
        }
    }
    else{
        printf("not allowed type!\n");
        abort();
    }
    free(algo);
    return NULL;
}

void group_get_exact_piece_ppa(group *gp, uint32_t lba, uint32_t set_idx, group_read_param *grp, bool isstart, lower_info *li, void (*cache_insert)(group *gp, uint32_t *piece_ppa)){
    if(lba==test_key){
        //GDB_MAKE_BREAKPOINT;
        //print_all_level(gp->level_list);
    }

    if(isstart){
        grp->gp=gp;
        grp->lba=lba;
        grp->value=NULL;
        grp->read_done=false;
        grp->seg=NULL;
        grp->type=GRP_TYPE::GP_WRITE;
        grp->retry_flag=NOT_RETRY;
    }

    if(gp->ppa!=INITIAL_STATE_PADDR){
        if (gp->cache_flag == CACHE_FLAG::UNCACHED){
            gp->cache_flag == CACHE_FLAG::FLYING;
            gp->pending_request.push_back(grp);
            /*issue read request*/
            grp->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
            send_IO_back_req(MAPPINGR, li, gp->ppa, grp->value, (void *)grp, group_read_end_req);
            return;
        }
        else if (gp->cache_flag == CACHE_FLAG::FLYING){
            if(grp->read_done){
                /*header request*/
                cache_insert(gp, (uint32_t*)grp->value->value);
                gp->cache_flag=CACHE_FLAG::CACHED;
                std::list<group_read_param*>::iterator pending_iter;
                for(pending_iter=gp->pending_request.begin(); pending_iter!=gp->pending_request.end(); pending_iter++){
                    if((*pending_iter)->type==GRP_TYPE::GP_READ){
                        printf("must be GRP write type!\n");
                        GDB_MAKE_BREAKPOINT;
                    }
                    (*pending_iter)->read_done=true;
                }
            }
            else{
                gp->pending_request.push_back(grp);
                return;
            }
        }
    }

    if(gp->ppa!=INITIAL_STATE_PADDR && gp->cache_flag!=CACHE_FLAG::CACHED){
        printf("gp must be in cached!\n");
        GDB_MAKE_BREAKPOINT;
    }

    if(grp->seg==NULL){
        grp->seg=group_get_segment(gp, lba);
        grp->read_done=false;

        if(grp->seg==NULL){ //not inserted
            grp->piece_ppa=INITIAL_STATE_PADDR;
            grp->read_done=true;
            grp->retry_flag=DATA_FOUND;
            if(grp->value){
                inf_free_valueset(grp->value, FS_MALLOC_R);
            }
        }
        else{
            if(grp->seg->original_start==lba){
                grp->piece_ppa=grp->seg->start_piece_ppa;
                grp->read_done=true;
                grp->retry_flag=DATA_FOUND;
                if(grp->value){
                    inf_free_valueset(grp->value, FS_MALLOC_R);
                }
            }
            else{
                grp_setup(grp, gp, grp->seg, lba);
                grp->piece_ppa = segment_get_addr(grp->seg, lba);
                if (grp->seg->type == SEGMENT_TYPE::APPROXIMATE){
                    if(grp->value==NULL){
                        grp->value = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
                    }
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

void group_monitor_print(){
    printf("total created segment: %lu\n", gm.total_segment);
    printf("average compaction segment:%.2lf\n", (double)gm.total_segment/ gm.compaction_cnt);
}