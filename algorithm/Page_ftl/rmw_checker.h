#include "../../include/container.h"
#include "../../include/sem_lock.h"
#include <stdlib.h>
#include <map>

typedef struct rmw_node{
    uint32_t lba;
    uint32_t global_seq;
    uint32_t offset;
    uint32_t length;
    fdriver_lock_t lock;
    char *value;
}rmw_node;

typedef struct rmw_checker{
    fdriver_lock_t lock;
    std::multimap<uint32_t, rmw_node*> rmw_set;
}_rmw_checker;

void rmw_node_init();
void rmw_node_insert(uint32_t lba, uint32_t offset, uint32_t length, uint32_t global_seq, char *value);//before read
bool rmw_node_merge(uint32_t lba, uint32_t global_seq, char *target_value);
uint32_t rmw_node_pick(uint32_t lba, uint32_t global_seq, char *target_value);
bool rmw_check(uint32_t lba);

void rmw_node_read_done(uint32_t lba, uint32_t global_seq);// after read
void rmw_node_delete(uint32_t lab, uint32_t global_seq); //after write
void rmw_node_free();