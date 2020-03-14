#ifndef __DFTL_LRU_LIST__
#define __DFTL_LRU_LIST__

#include <stdio.h>
#include <stdlib.h>

typedef struct __node{
	void *DATA;
	struct __node *next;
	struct __node *prev;
} NODE;

typedef struct __lru{
	int size;
	NODE *head;
	NODE *tail;
} LRU;

//lru
void lru_init(LRU**);
void lru_free(LRU*);
NODE* lru_push(LRU*, void*);
void* lru_pop(LRU*);
void lru_update(LRU*, NODE*);
void lru_delete(LRU*, NODE*);

#endif
