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


CRB *crb_init();
CRB_node *crb_find_node(CRB *crb, uint32_t lba);
void crb_insert(CRB *crb, temp_map *tmap, segment *seg, std::vector<CRB_node> *update_target); //remove overlapped lba
void crb_free(CRB *crb);
void crb_remove_overlap(CRB *crb, temp_map *tmap, std::vector<CRB_node>* update_target);
uint64_t crb_size(CRB *crb);