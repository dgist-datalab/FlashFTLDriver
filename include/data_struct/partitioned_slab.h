#pragma once
#include <map>
#include <pthread.h>
#include <vector>
#include <queue>
#include <string.h>
#include "../settings.h"
#include "../types.h"

#define SLABSIZE PAGESIZE


typedef struct partitioned_slab_master{
    uint64_t block_in_partition;
    uint64_t max_slab_num;

    std::vector<std::map<uint64_t, uint64_t>> partitiones;
    std::vector<pthread_mutex_t> partition_locks;
    pthread_mutex_t queue_lock;

    char **slabs;
    uint8_t *slab_deref_count;
    std::queue<uint64_t> free_slabs;    
} PS_master;

PS_master *PS_master_init(uint64_t partition_num, uint64_t block_in_partition, uint64_t max_slab_num);
void PS_master_destroy(PS_master *master);
void PS_master_insert(PS_master *master, int key, char *value);
void PS_master_free_partition(PS_master *master, int key);
void PS_master_free_slab(PS_master *master, uint64_t key);
char *PS_master_get(PS_master *master, int key);
bool PS_ismeta_data(uint32_t type);
