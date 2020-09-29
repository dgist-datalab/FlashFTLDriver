#include "lru_list.h"

void lru_init(LRU** lru, void (*data_free)(void*)){
	*lru = (LRU*)malloc(sizeof(LRU));
	(*lru)->size=0;
	(*lru)->head = (*lru)->tail = NULL;
	(*lru)->free_data=data_free;
}

void lru_free(LRU* lru){
	while(lru_pop(lru)){}
	free(lru);
}

lru_node* lru_push(LRU* lru, void* table_ptr){
	lru_node *now = (lru_node*)malloc(sizeof(lru_node));
	now->data = table_ptr;
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

lru_node* lru_push_last(LRU* lru, void* table_ptr){
	lru_node *now = (lru_node*)malloc(sizeof(lru_node));
	now->data = table_ptr;
	now->next = now->prev = NULL;
	if(lru->size == 0){
		lru->head = lru->tail = now;
	}
	else{
		lru->tail->next = now;
		now->prev = lru->tail;
		lru->tail = now;
	}
	lru->size++;
	return now;
}

void* lru_pop(LRU* lru){
	if(!lru->head || lru->size == 0){
		return NULL;
	}
	lru_node *now = lru->tail;
	void *re = now->data;
	lru->tail = now->prev;
	if(lru->tail != NULL){
		lru->tail->next = NULL;
	}
	else{
		lru->head = NULL;
	}
	lru->size--;
	if(lru->free_data){
		lru->free_data(re);
	}
	free(now);
	return re;
}

void lru_update(LRU* lru, lru_node* now){
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

void lru_delete(LRU* lru, lru_node* now){
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
	if(lru->free_data){
		lru->free_data(now->data);
	}
	free(now);
}
