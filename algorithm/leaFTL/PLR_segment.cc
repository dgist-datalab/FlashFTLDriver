#include "./PLR_segment.h"
#include "../../include/settings.h"
#include "../../include/debug_utils.h"
extern uint32_t test_key;
segment *segment_make(temp_map *map, SEGMENT_TYPE type, uint32_t interval){
    segment *res=(segment *)malloc(sizeof(segment));
    res->type=type;
    res->start=map->lba[0];
    res->original_start=res->start;
    res->end=map->lba[map->size-1];
    res->start_piece_ppa=map->piece_ppa[0];

    if(map->lba[0]==test_key-1 && map->piece_ppa[0]==3874){
        GDB_MAKE_BREAKPOINT;
        /*
        printf("test debug\n");
        for(uint32_t i=0; i<map->size; i++){
            printf("\t%u:%u\n", map->lba[i], map->piece_ppa[i]);
        }
        */
    }

    if(type==SEGMENT_TYPE::ACCURATE){
        res->body.interval=interval;
        if(segment_acc_include(res, test_key)){
            printf("[DEBUG] %u is ACC type, original_start:%u (%p)\n", test_key, res->original_start, res);
        }
    }
    else{
        res->body.plr=new PLR(7, 5);
        for(uint32_t i=0; i<map->size; i++){
           if(map->lba[i]==test_key){
                printf("[DEBUG] %u is APP type, original_start:%u (%p)\n", test_key, res->original_start, res);
            }
            res->body.plr->insert(map->lba[i]-res->start, map->piece_ppa[i]/L2PGAP);
        }
        res->body.plr->insert_end();
    }
    res->level_ptr=NULL;
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
            res=(lba-seg->original_start)/(seg->body.interval<=1?1:seg->body.interval)+seg->start_piece_ppa;
            break;
        case SEGMENT_TYPE::APPROXIMATE:
            res=seg->body.plr->get(lba-seg->original_start) *L2PGAP;
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
        if(old_seg->body.interval==0){
            return segment_acc_include(new_seg, old_seg->start);
        }
        else{
            if(old_seg->body.interval == new_seg->body.interval && (old_seg->start-new_seg->start)%new_seg->body.interval==0){
                return true;
            }
            return false;
        }
    }
    else{
        return false;
    }
}


void segment_update(segment *seg, uint32_t start, uint32_t end){
    if(seg->start <= start){
        seg->start=start;
    }
    else{
        printf("to the wide range is unavailabe!\n");
        abort();
    }

    if(seg->end>=end){
        seg->end=end;
    }
    else{
        printf("to the wide range is unavailabe!\n");
        abort();
    }
}

uint64_t segment_size(segment *seg){
    if(seg->type==SEGMENT_TYPE::ACCURATE){
        return 8;
    }
    else{
        return seg->body.plr->get_line_cnt()*8;
    }
}

void segment_print(segment *seg){
    printf("%u~%u,%s\n", seg->start, seg->end, seg->type==SEGMENT_TYPE::ACCURATE?"ACC":"APP");
}