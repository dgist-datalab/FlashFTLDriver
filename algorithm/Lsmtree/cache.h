#ifndef __CACHE_H__
#define __CACHE_H__
#include"skiplist.h"
#include "level.h"
#include<pthread.h>
typedef struct cache_entry{
	struct run* entry;
	struct cache_entry *up;
	struct cache_entry *down;
	bool locked;
	int dmatag;
}cache_entry;

typedef struct cache{
	int m_size;
	int n_size;
	int max_size;
	int locked_entry;
	cache_entry *top;
	cache_entry *bottom;
	pthread_mutex_t cache_lock;
}cache;

cache *cache_init(uint32_t);
struct run* cache_get(cache *c);
cache_entry* cache_insert(cache *, struct run *, int );
bool cache_insertable(cache *c);
bool cache_delete(cache *, struct run *);
bool cache_delete_entry_only(cache *c, struct run *ent);
void cache_update(cache *, struct run *);
void cache_evict(cache *);
void cache_size_update(cache *c, int m_size);
void cache_free(cache *);
void cache_print(cache *);
void cache_entry_lock(cache *,cache_entry *);
void cache_entry_unlock(cache *,cache_entry *);
#endif
