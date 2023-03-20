#include "./CRB.h"
#include "../../include/debug_utils.h"
#include <algorithm>
extern bool leaFTL_debug;
CRB *crb_init(){
    CRB *res=new CRB();
    return res;
}

CRB_iter crb_find_start_ptr(CRB *crb, uint32_t lba){
    return crb->begin();
}

/*if no target segment,  return NULL*/
CRB_node *crb_find_node(CRB *crb, uint32_t lba){
    CRB_iter iter=crb_find_start_ptr(crb, lba);
    CRB_node *res=NULL;
    for(;iter!=crb->end(); iter++){
        if(iter->first > lba) break;
        if(iter->second->lba_arr->back() < lba) continue;
        bool found=binary_search(iter->second->lba_arr->begin(), iter->second->lba_arr->end(), lba);
        if(found){
            res=iter->second;
            break;
        }
    }
    return res;
}


uint32_t lowerBound(uint32_t arr[], uint32_t n, uint32_t target){
    auto it = std::lower_bound(arr, arr + n, target);
    if (it != arr + n) {
        return it - arr;
    }
    else {
        return UINT32_MAX; // lower bound not found
    }
}

lba_buffer* remove_overlap_CRBnode(lba_buffer *lba_arr, temp_map *tmap){
    uint32_t start_idx=lowerBound(tmap->lba, tmap->size, lba_arr->at(0));
    if(start_idx==UINT32_MAX){
        printf("not overlap!! %s:%d\n", __FUNCTION__, __LINE__);
        abort();
    }

    lba_buffer *res=new lba_buffer();
    lba_buffer_iter iter=lba_arr->begin();
    uint32_t ptr=start_idx;
    for(;iter!=lba_arr->end() && ptr<tmap->size; iter++){
        while((*iter) > tmap->lba[ptr] && ptr<tmap->size-1){
            ptr++;
        }

        if((*iter)==tmap->lba[ptr]){
            //not insert;
            ptr++;
            continue;
        }
        else if((*iter)<tmap->lba[ptr]){
            res->push_back((*iter));
        }
        else{
            if(ptr!=tmap->size-1){
                printf("the while pharse must be error! %s:%d\n", __FUNCTION__, __LINE__);
                abort();
            }
            break;
        }
    }

    //insert remain lbas
    for(;iter!=lba_arr->end(); iter++){
        res->push_back(*iter);
    }

#ifdef DEBUG
    //overlap re-check
    uint32_t j=0;
    for(uint32_t i=0; i<tmap->size; i++){
        uint32_t target_lba=tmap->lba[i];
        for(;j<res->size(); j++){
            if(res->at(j) > target_lba) break;
            if(res->at(j)==target_lba){
                printf("the overlap does not removed! %s:%d\n", __FUNCTION__, __LINE__);
                abort();
            } 
        }
    }
#endif
    return res;
}

void crb_remove_overlap(CRB *crb, temp_map *tmap, std::vector<CRB_node>* update_target){
    uint32_t end=tmap->lba[tmap->size-1];
    CRB_iter iter=crb_find_start_ptr(crb, tmap->lba[0]);
    std::vector<CRB_node*> new_target;
    new_target.clear();
    CRB_node update_node;
    for(;iter!=crb->end();){
        if(iter->first > end ) break;
        if(iter->second->lba_arr->back() < tmap->lba[0]){
            iter++;
            continue;
        }
        lba_buffer *temp=remove_overlap_CRBnode(iter->second->lba_arr, tmap);
        if(temp){
            if(temp->size()==0){ //remove CRNODE
                update_node.lba_arr=NULL;
                update_node.seg=iter->second->seg;
                update_target->push_back(update_node);

                delete temp;
                delete iter->second->lba_arr;
                free(iter->second);
                crb->erase(iter++);
            }
            else if(iter->first!=temp->at(0)){
                delete iter->second->lba_arr;
                iter->second->lba_arr=temp;
                new_target.push_back(iter->second);
                update_target->push_back(*iter->second);
                crb->erase(iter++);
            }
            else{
                delete iter->second->lba_arr;
                iter->second->lba_arr=temp;
                update_target->push_back(*iter->second);
                iter++;
            }
        }
    }

    for(uint32_t i=0; i<new_target.size(); i++){
        crb->insert(std::pair<uint32_t, CRB_node*>(new_target[i]->lba_arr->at(0), new_target[i]));
    }
}

void crb_insert(CRB *crb, temp_map *tmap, segment *seg, std::vector<CRB_node> *update_target){
    CRB_node *new_node=(CRB_node*)malloc(sizeof(CRB_node));
    new_node->lba_arr=new lba_buffer(tmap->lba, tmap->lba+tmap->size);
    new_node->seg=seg;

    crb_remove_overlap(crb, tmap, update_target);

    std::pair<CRB_iter, bool> temp=crb->insert(std::pair<uint32_t, CRB_node*>(seg->start, new_node));
    if(temp.second==false){
        printf("overlaps  are not removed %s:%u\n", __FUNCTION__, __LINE__);
        abort();
    }
}

void crb_free(CRB *crb){
    CRB_iter iter=crb->begin();
    for(;iter!=crb->end();){
        free(iter->second);
        crb->erase(iter++);
    }
    delete crb;
}

uint64_t crb_size(CRB *crb){
    uint64_t res=0;
    CRB_iter iter=crb->begin();
    for(; iter!=crb->end(); iter++){
        res+=iter->second->lba_arr->size()*4;
    }
    return res;
}