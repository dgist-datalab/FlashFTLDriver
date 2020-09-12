#ifndef __DFTL_LRU_LIST__
#define __DFTL_LRU_LIST__

#include <stdio.h>
#include <stdlib.h>

typedef struct lru_node{
	void *data;
	struct lru_node *next;
	struct lru_node *prev;
} lru_node;

typedef struct __lru{
	int size;
	lru_node *head;
	lru_node *tail;
	void (*free_data)(void *);
} LRU;

//lru
void lru_init(LRU**, void(*)(void*));
void lru_free(LRU*);
lru_node* lru_push(LRU*, void*);
void* lru_pop(LRU*);
void lru_update(LRU*, lru_node*);
void lru_delete(LRU*, lru_node*);

#define for_each_lru_backword(lru, lru_node)\
	for(lru_node=lru->tail; lru_node!=NULL; lru_node=lru_node->prev)

#define for_each_lru_list(lru, lru_node)\
	for(lru_node=lru->head; lru_node!=NULL; lru_node=lru_node->next)

#endif
