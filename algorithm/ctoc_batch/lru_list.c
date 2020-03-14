#include "lru_list.h"

void lru_init(LRU** lru){
	*lru = (LRU*)malloc(sizeof(LRU));
	(*lru)->size=0;
	(*lru)->head = (*lru)->tail = NULL;
}

void lru_free(LRU* lru){
	while(lru_pop(lru)){}
	free(lru);
}

NODE* lru_push(LRU* lru, void* table_ptr){
	NODE *now = (NODE*)malloc(sizeof(NODE));
	now->DATA = table_ptr;
	now->next = now->prev = NULL;
	if(lru->size == 0){
		lru->head = lru->tail = now;
	}
	else{
		lru->head->prev = now;
		now->next = lru->head;
		lru->head = now;
	}
	lru->size++;
	return now;
}

void* lru_pop(LRU* lru){
	if(!lru->head || lru->size == 0){
		return NULL;
	}
	NODE *now = lru->tail;
	void *re = now->DATA;
	lru->tail = now->prev;
	if(lru->tail != NULL){
		lru->tail->next = NULL;
	}
	else{
		lru->head = NULL;
	}
	lru->size--;
	free(now);
	return re;
}

void lru_update(LRU* lru, NODE* now){
	if(now == NULL){
		return ;
	}
	if(now == lru->head){
		return ;
	}
	if(now == lru->tail){
		lru->tail = now->prev;
		lru->tail->next = NULL;
	}
	else{
		now->prev->next = now->next;
		now->next->prev = now->prev;
	}
	now->prev = NULL;
	lru->head->prev = now;
	now->next = lru->head;
	lru->head = now;
}

void lru_delete(LRU* lru, NODE* now){
	if(now == NULL){
		return ;
	}
	if(now == lru->head){
		lru->head = now->next;
		if(lru->head != NULL){
			lru->head->prev = NULL;
		}
		else{
			lru->tail = NULL;
		}
	}
	else if(now == lru->tail){
		lru->tail = now->prev;
		lru->tail->next = NULL;
	}
	else{
		now->prev->next = now->next;
		now->next->prev = now->prev;
	}	
	lru->size--;
	free(now);
}
