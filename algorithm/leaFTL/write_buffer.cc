#include "./write_buffer.h"
#include "./page_manager.h"

lea_write_buffer *lea_write_buffer_init(uint32_t max_write_buffer_byte){
    lea_write_buffer *res;
    res=(lea_write_buffer*)calloc(1, sizeof(lea_write_buffer));
    uint32_t number_of_sectors=max_write_buffer_byte/PAGESIZE*L2PGAP;
    res->sector_buffer=(char**)malloc(sizeof(char*)*number_of_sectors);
    for(uint32_t i=0; i<number_of_sectors; i++){
        res->sector_buffer[i]=(char*)malloc(LPAGESIZE);
    }

    res->L2P_map.clear();
    g_buffer_init(&res->page_buf);
    res->max_ps_ptr=number_of_sectors;
    res->ps_ptr=0;
    return res;
}

bool lea_write_buffer_flush(lea_write_buffer *wb, page_manager *pm, temp_map *tmap, uint32_t page_size){
    bool res;
    std::map<uint32_t, uint32_t>::iterator iter;
    /*making map*/
    
    uint32_t target_flush_size=MIN(page_size*L2PGAP, wb->L2P_map.size());
    res=target_flush_size >= wb->L2P_map.size();
    if(page_size==0) return res;

    uint32_t i=0;
    char **sector_buffer=wb->sector_buffer;
    uint32_t piece_ppa_res[L2PGAP];

    if(target_flush_size%L2PGAP){
        printf("target_fulsh_size must be divided in L2PGAP without the res!\n");
        abort();
    }

    for(iter=wb->L2P_map.begin(); iter!=wb->L2P_map.end() && i<target_flush_size; i++){
        /*flushing data*/
        g_buffer_insert(&wb->page_buf, sector_buffer[iter->second], iter->first);
        if(wb->page_buf.idx==L2PGAP){
            pm_page_flush(pm, true, DATAW, wb->page_buf.key, wb->page_buf.value, L2PGAP, piece_ppa_res);
            g_buffer_to_temp_map(&wb->page_buf, tmap, piece_ppa_res);
        }
        wb->L2P_map.erase(iter++);
    }
    return res;
}

void lea_write_buffer_clear(lea_write_buffer *wb){
    wb->ps_ptr=0;
    wb->L2P_map.clear();   
}

bool lea_write_buffer_isfull(lea_write_buffer *wb){
    return wb->ps_ptr == wb->max_ps_ptr;
}

uint32_t lea_write_buffer_insert(lea_write_buffer *wb, uint32_t lba, char *data){
    std::map<uint32_t, uint32_t>::iterator iter=wb->L2P_map.find(lba);
    char *target_ptr;
    if(iter!=wb->L2P_map.end()){
        target_ptr=wb->sector_buffer[iter->second];
    }
    else{
        wb->L2P_map.insert(std::pair<uint32_t, uint32_t>(lba, wb->ps_ptr));
        target_ptr=wb->sector_buffer[wb->ps_ptr++];
    }

    memcpy(target_ptr, data, LPAGESIZE);
    return 0;
}

char * lea_write_buffer_get(lea_write_buffer *wb, uint32_t lba){
    std::map<uint32_t, uint32_t>::iterator iter=wb->L2P_map.find(lba);
    if(iter!=wb->L2P_map.end()){
        return wb->sector_buffer[iter->second];
    }
    else{
        return NULL;
    }
}

void lea_write_buffer_free(lea_write_buffer *wb){
    wb->L2P_map.clear();
    g_buffer_free(&wb->page_buf);
    for(uint32_t i=0; i<wb->max_ps_ptr; i++){
        free(wb->sector_buffer[i]);
    }
    free(wb->sector_buffer);
    free(wb);
}


uint32_t lea_write_buffer_total_LBA_distance(lea_write_buffer *wb){
    uint32_t res=0;
    std::map<uint32_t, uint32_t>::iterator iter;
    uint32_t prev_lba=-1;
    for(iter=wb->L2P_map.begin(); iter!=wb->L2P_map.end(); iter++){
        if(prev_lba!=-1){
            res+=iter->first-prev_lba;
        }
        else{
            prev_lba=iter->first;
        }
    }
    return res;
}