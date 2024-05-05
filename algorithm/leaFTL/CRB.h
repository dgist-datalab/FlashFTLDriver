#pragma once
#include "./PLR_segment.h"
#include <vector>
#include <map>
typedef std::vector<uint32_t> lba_buffer;
typedef std::vector<uint32_t>::iterator lba_buffer_iter;
typedef struct CRB_node{
    lba_buffer *lba_arr;
    segment *seg;
}CRB_node;

typedef std::map<uint32_t, CRB_node *> CRB;
typedef std::map<uint32_t, CRB_node *>::iterator CRB_iter;
typedef std::map<uint32_t, CRB_node *>::reverse_iterator CRB_riter;


void master_crb_init();
void master_crb_clean(uint32_t group_idx);
void master_crb_insert(uint32_t *lba, uint32_t size, uint32_t group_idx);
void master_crb_remove(uint32_t *lba, uint32_t size, uint32_t group_idx);
CRB *crb_init();
CRB_node *crb_find_node(CRB *crb, uint32_t lba);
void crb_insert(CRB *crb, temp_map *tmap, segment *seg, std::vector<CRB_node> *update_target, uint32_t group_idx); //remove overlapped lba
void crb_free(CRB *crb);
void crb_remove_overlap(CRB *crb, temp_map *tmap, std::vector<CRB_node>* update_target, uint32_t group_idx);
uint64_t crb_size(CRB *crb, uint32_t group_id);