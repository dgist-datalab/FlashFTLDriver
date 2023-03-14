#include "./PLR_segment.h"
#include "../../include/settings.h"
extern uint32_t test_key;
segment *segment_make(temp_map *map, SEGMENT_TYPE type, uint32_t interval){
    segment *res=(segment *)malloc(sizeof(segment));
    res->type=type;
    res->start=map->lba[0];
    res->end=map->lba[map->size-1];
    res->start_piece_ppa=map->piece_ppa[0];

    if(type==SEGMENT_TYPE::ACCURATE){
        res->body.interval=interval;
        if(segment_acc_include(res, test_key)){
            printf("[DEBUG] %u is ACC type\n", test_key);
        }
    }
    else{
        res->body.plr=new PLR(7, 5);
        for(uint32_t i=0; i<map->size; i++){
           if(map->lba[i]==test_key){
                printf("[DEBUG] %u is APP type\n", test_key);
            }
            res->body.plr->insert(map->lba[i]-res->start, map->piece_ppa[i]/L2PGAP);
        }
        res->body.plr->insert_end();
    }
    return res;
}

bool segment_acc_include(segment *acc_seg, uint32_t lba){
    if(acc_seg->type!=SEGMENT_TYPE::ACCURATE){
        printf("this function(%s) must be called by ACC type\n", __FUNCTION__);
        abort();
    }

    if(acc_seg->end < lba || acc_seg->start > lba){
        return false;
    }
    else if (acc_seg->body.interval!=0 && (lba-acc_seg->start)%acc_seg->body.interval){
        return false;
    }
    return true;
}

uint32_t segment_get_addr(segment *seg, uint32_t lba){
    uint32_t res=UINT32_MAX;
    switch(seg->type){
        case SEGMENT_TYPE::ACCURATE:
            res=(lba-seg->start)/(seg->body.interval<=1?1:seg->body.interval)+seg->start_piece_ppa;
            break;
        case SEGMENT_TYPE::APPROXIMATE:
            res=seg->body.plr->get(lba-seg->start) *L2PGAP;
            //res+=seg->start_piece_ppa;
            break;
    }
    return res;
}

void segment_free(segment* seg){
    switch (seg->type){
        case SEGMENT_TYPE::ACCURATE:
           break;
        case SEGMENT_TYPE::APPROXIMATE:
            delete seg->body.plr;
            break;
    }
    free(seg);
}

/*new seg must be ACCTYPE*/
bool segment_removable(segment *old_seg, segment *new_seg){
    if(new_seg->type!=SEGMENT_TYPE::ACCURATE){
        printf("not allowed type %s:%u\n", __FUNCTION__, __LINE__);
        abort();
    }

    /*check all overlap*/
    if(old_seg->start >= new_seg->start && old_seg->end<=new_seg->end){
        return true;
    }
    else{
        return false;
    }
}