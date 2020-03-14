#include "lsmtree.h"
#include "../../include/lsm_settings.h"
#include "../../include/utils/debug_tools.h"
#include "../../include/utils/kvssd.h"
#include "cache.h"
#include<stdlib.h>
#include<string.h>
#include<stdio.h>
int32_t update,delete_,insert;
extern lsmtree LSM;
cache *cache_init(uint32_t noe){
	cache *c=(cache*)malloc(sizeof(cache));
	c->m_size=noe;
	c->n_size=0;
	c->top=NULL;
	c->bottom=NULL;
	c->max_size=noe;
	pthread_mutex_init(&c->cache_lock,NULL);
	return c;
}

void cache_evict(cache *c){
	cache_delete(c,cache_get(c));
}

void cache_size_update(cache *c, int m_size){
	if(m_size <0) return;
	if(c->n_size>m_size){
		int i=0;
		int target=c->n_size-m_size;
		for(i=0; i<target; i++){
			cache_evict(c);
		}
	}
	c->m_size=m_size>c->max_size?c->max_size:m_size;
}

cache_entry * cache_insert(cache *c, run_t *ent, int dmatag){
	if(!c->m_size) return NULL;
	if(c->m_size < c->n_size){
		int target=c->n_size-c->m_size+1;
		for(int i=0; i<target; i++){
			if(!cache_delete(c,cache_get(c))) return NULL;
		}
	}

	insert++;
	cache_entry *c_ent=(cache_entry*)malloc(sizeof(cache_entry));

	c_ent->locked=false;
	c_ent->entry=ent;
	if(!LSM.nocpy)ent->cache_data->iscached=2;
	if(c->bottom==NULL){
		c->bottom=c_ent;
		c->top=c_ent;
		c->bottom->down=NULL;
		c->top->up=NULL;
		c->n_size++;
		return c_ent;
	}

	c->top->up=c_ent;
	c_ent->down=c->top;

	c->top=c_ent;
	c_ent->up=NULL;
	c->n_size++;
	//printf("cache insert:%d\n",c->n_size);
	return c_ent;
}
bool cache_delete(cache *c, run_t * ent){
	delete_++;
	if(c->n_size==0 || !ent){
		return false;
	}
	//printf("cache delete\n");
	cache_entry *c_ent=ent->c_entry;	
	if(c_ent==c->bottom){
		c->bottom=c_ent->up;
	}else if(c_ent==c->top){
		c->top=c_ent->down;
	}
	if(!LSM.nocpy)htable_free(ent->cache_data);
	c->n_size--;
	free(c_ent);
	ent->c_entry=NULL;
	return true;
}

bool cache_delete_entry_only(cache *c, run_t *ent){
	if(c->n_size==0){
		return false;
	}
	cache_entry *c_ent=ent->c_entry;
	if(c_ent==NULL) {
		return false;
	}
	if(c->bottom==c->top && c->top==c_ent){
		c->top=c->bottom=NULL;
	}
	else if(c->top==c_ent){
		cache_entry *down=c_ent->down;
		down->up=NULL;
		c->top=down;
	}
	else if(c->bottom==c_ent){
		cache_entry *up=c_ent->up;	
		up->down=NULL;
		c->bottom=up;
	}
	else{
		cache_entry *up=c_ent->up;
		cache_entry *down=c_ent->down;
		
		up->down=down;
		down->up=up;
	}
	c->n_size--;
	free(c_ent);
	ent->c_entry=NULL;
	return true;
}

void cache_update(cache *c, run_t* ent){
	update++;
	cache_entry *c_ent=ent->c_entry;
	if(c->top==c_ent){ 
		return;
	}
	if(c->bottom==c_ent){
		cache_entry *up=c_ent->up;
		up->down=NULL;
		c->bottom=up;
	}
	else{
		cache_entry *up=c_ent->up;
		cache_entry *down=c_ent->down;
		up->down=down;
		down->up=up;
	}

	c->top->up=c_ent;
	c_ent->up=NULL;
	c_ent->down=c->top;
	c->top=c_ent;

	if(c->m_size < c->n_size){
		int target=c->n_size-c->m_size+1;
		for(int i=0; i<target; i++){
			cache_delete(c,cache_get(c));
		}
	}
}

run_t* cache_get(cache *c){
	if(c->n_size==0){
		return NULL;
	}

//	cache_entry *res=c->bottom;
//	cache_entry *up=res->up;
	int c_cnt=1;
	cache_entry *res=c->bottom, *up;
	while(res && res->locked){
		res=res->up; 
		c_cnt=0;
	}
	if(res)
		up=res->up;
	else 
		return NULL;

	if(up==NULL && c_cnt){
		c->bottom=c->top=NULL;
	}
	else if(c_cnt && res->locked){
		return NULL;
	}
	else{
		if(res==c->bottom){	
			up->down=NULL;
			c->bottom=up;
		}else{
			if(up){
				up->down=res->down;
				res->down->up=up;
			}else res->down->up=NULL;
		}
	}

	if(!res->entry->c_entry || res->entry->c_entry!=res){
		cache_print(c);
		printf("hello\n");
	}
	return res->entry;
}
void cache_free(cache *c){
	run_t *tmp_ent;
	printf("cache size:%d %d %d\n",c->n_size,c->m_size,c->max_size);
	while((tmp_ent=cache_get(c))){
		free(tmp_ent->c_entry);
		tmp_ent->c_entry=NULL;
		c->n_size--;
	}
	free(c);
	printf("insert:%u delete:%d update:%u\n",insert,delete_,update);
}
int print_number;
void cache_print(cache *c){
	cache_entry *start=c->top;
	print_number=0;
	run_t *tent;
	while(start!=NULL){
		tent=start->entry;
		if(start->entry->c_entry!=start){
			printf("fuck!!!\n");
		}
#ifdef KVSSD
		if(LSM.nocpy){
			//printf("[%d]c->endtry->key:%s c->entry->pbn:%lu d:%p\n",print_number++,kvssd_tostring(tent->key),tent->pbn,tent->cache_nocpy_data_ptr);
		}
#else
		//printf("[%d]c->entry->key:%d c->entry->pbn:%d d:%p\n",print_number++,tent->key,tent->pbn,tent->cache_data);
#endif
		start=start->down;
	}
}

bool cache_insertable(cache *c){
	//printf("m:n %d:%d\n", c->m_size, c->n_size);
	return c->m_size==0?0:1;
}

void cache_entry_lock(cache *c, cache_entry *entry){
	c->locked_entry++;
	entry->locked=true;
}

void cache_entry_unlock(cache *c, cache_entry *entry){
	c->locked_entry--;
	entry->locked=false;
}
