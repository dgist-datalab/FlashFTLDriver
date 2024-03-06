#include "./partitioned_slab.h"

PS_master *PS_master_init(uint64_t partition_num, uint64_t block_in_partition, uint64_t max_slab_num){
    PS_master *master = new PS_master();
    master->block_in_partition = block_in_partition;
    master->max_slab_num = max_slab_num;

    master->free_slabs=std::queue<uint64_t>();
    master->partitiones=std::vector<std::map<uint64_t, uint64_t>>(partition_num);
    master->partition_locks=std::vector<pthread_mutex_t>(partition_num);

    for(int i = 0; i < partition_num; i++){
        pthread_mutex_init(&master->partition_locks[i], NULL);
    }

    pthread_mutex_init(&master->queue_lock, NULL);


    master->slabs = (char **)malloc(sizeof(char *) * max_slab_num);
    master->slab_deref_count=(uint8_t *)malloc(sizeof(uint8_t) * max_slab_num);
    memset(master->slab_deref_count, 0, sizeof(uint8_t) * max_slab_num);
    for(int i=0; i<max_slab_num; i++){
        master->slabs[i] = (char *)malloc(SLABSIZE);
        master->free_slabs.push(i);
    }
    return master;
}

void PS_master_destroy(PS_master *master){
    for(int i=0; i<master->max_slab_num; i++){
        free(master->slabs[i]);
    }
    free(master->slabs);
    for(int i=0; i<master->partition_locks.size(); i++){
        pthread_mutex_destroy(&master->partition_locks[i]);
    }
    delete master;
}

void PS_master_insert(PS_master *master, int key, char *value){
    uint64_t partition = key / master->block_in_partition;
    pthread_mutex_lock(&master->partition_locks[partition]);

    pthread_mutex_lock(&master->queue_lock);
    if(master->free_slabs.empty()){
        printf("empty slab space!!\n");
        abort();
        return;
    }
    uint64_t slab_num = master->free_slabs.front();
    master->free_slabs.pop();
    pthread_mutex_unlock(&master->queue_lock);

    master->partitiones[partition][key] = slab_num;
    pthread_mutex_unlock(&master->partition_locks[partition]);
    memcpy(master->slabs[slab_num], value, SLABSIZE);
}

char *PS_master_get(PS_master *master, int key){
    uint64_t partition = key / master->block_in_partition;
    pthread_mutex_lock(&master->partition_locks[partition]);
    auto it=master->partitiones[partition].find(key);
    if (it== master->partitiones[partition].end())
    {
        pthread_mutex_unlock(&master->partition_locks[partition]);
        return NULL;
    }
    pthread_mutex_unlock(&master->partition_locks[partition]);
    return master->slabs[it->second];
}

void PS_master_free_partition(PS_master *master, int key){
    uint64_t partition = key / master->block_in_partition;
    pthread_mutex_lock(&master->partition_locks[partition]);
    pthread_mutex_lock(&master->queue_lock);
    for(auto it = master->partitiones[partition].begin(); it != master->partitiones[partition].end(); it++){
        master->free_slabs.push(it->second);
    }
    pthread_mutex_unlock(&master->queue_lock);
    master->partitiones[partition].clear();
    pthread_mutex_unlock(&master->partition_locks[partition]);
}

void PS_master_free_slab(PS_master*master, uint64_t key){
    uint64_t partition = key / master->block_in_partition;
    pthread_mutex_lock(&master->partition_locks[partition]);
    auto it=master->partitiones[partition].find(key);
    if (it== master->partitiones[partition].end())
    {
        pthread_mutex_unlock(&master->partition_locks[partition]);
        return;
    }
    master->slab_deref_count[it->second]++;
    if(master->slab_deref_count[it->second]==L2PGAP){
        pthread_mutex_lock(&master->queue_lock);
        master->free_slabs.push(it->second);
        pthread_mutex_unlock(&master->queue_lock);
        master->slab_deref_count[it->second]=0;
        master->partitiones[partition].erase(it);
    }
    pthread_mutex_unlock(&master->partition_locks[partition]);
}

bool PS_ismeta_data(uint32_t type){
	switch(type){
		case MAPPINGR:
		case MAPPINGW:
		case GCMR:
		case GCMR_DGC:
		case GCMW:
		case GCMW_DGC:
			return true;
		default:
			return false;
	}
}