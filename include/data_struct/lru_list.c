#include "lru_list.h"

void lru_check_error(LRU *lru){
	lru_node *temp=lru->head;
	int cnt=0;
	int error_code=0;
	if(!lru->head || !lru->tail){
		goto err;
	}
	while(temp){
		if(temp==lru->head){
			if(temp->next==NULL && temp!=lru->tail){
				error_code=1;
				goto err;
			}
		}
		else if(temp==lru->tail){
			if(temp->prev==NULL){
				error_code=2;
				goto err;
			}
		}
		else{
			if(temp->next==NULL){
				error_code=3;
				goto err;
			}
			if(temp->prev==NULL){
				error_code=4;
				goto err;
			}
		}
		temp=temp->next;
	}
	return;
err:
	printf("break %u\n",error_code);
	return;
}

void lru_init(LRU** lru, void (*data_free)(void*), uint32_t (*retrieve_key)(void*)){
	*lru = (LRU*)malloc(sizeof(LRU));
	(*lru)->size=0;
	(*lru)->head = (*lru)->tail = NULL;
	(*lru)->free_data=data_free;
	(*lru)->retrieve_key=retrieve_key;

	art_tree_init(&(*lru)->map);
}

void lru_free(LRU* lru){
	while(lru_pop(lru)){}
	art_tree_destroy(&lru->map);
	free(lru);
}

lru_node* lru_push_special(LRU* lru, void* table_ptr, uint32_t type, uint32_t id, uint32_t size){
	lru_node *now = (lru_node*)malloc(sizeof(lru_node));
	now->data = table_ptr;
	now->next = now->prev = NULL;
	now->type=type;
	now->id=id;
	now->size=size;
	if(lru->size == 0){
		lru->head = lru->tail = now;
	}
	else{
		lru->head->prev = now;
		now->next = lru->head;
		lru->head = now;
	}
	
	if(lru->retrieve_key){
		uint32_t key=lru->retrieve_key(table_ptr);
		if(art_insert(&lru->map, (const unsigned char*)&key, sizeof(key), now->data)){
			printf("already exist key:%u in lru map", key);
			abort();
		}
	}

	//lru_check_error(lru);
	lru->size++;
	return now;
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
	
	if(lru->retrieve_key){
		uint32_t key=lru->retrieve_key(table_ptr);
		if(art_insert(&lru->map, (const unsigned char*)&key, sizeof(key), now->data)){
			printf("already exist key:%u in lru map", key);
			abort();
		}
	}

	//lru_check_error(lru);
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

	if(lru->retrieve_key){
		uint32_t key=lru->retrieve_key(table_ptr);
		if(art_insert(&lru->map, (const unsigned char*)&key, sizeof(key), now->data)){
			printf("already exist key:%u in lru map", key);
			abort();
		}
	}

	lru->size++;
	//lru_check_error(lru);
	return now;
}


void* lru_pop_special(LRU* lru, uint32_t *type, uint32_t *idx, uint32_t *size){
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
	if(lru->retrieve_key){
		uint32_t key=lru->retrieve_key(now->data);
		art_delete(&lru->map, (const unsigned char*)&key, sizeof(key));
	}

	*type=now->type;
	*idx=now->id;
	*size=now->size;

	lru->size--;
	if(lru->free_data){
		lru->free_data(re);
	}

	
	free(now);
	return re;
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
	if(lru->retrieve_key){
		uint32_t key=lru->retrieve_key(now->data);
		art_delete(&lru->map, (const unsigned char*)&key, sizeof(key));
	}

	lru->size--;
	if(lru->free_data){
		lru->free_data(re);
	}

	
	free(now);
	//lru_check_error(lru);
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
		//lru_check_error(lru);
		now->prev->next = now->next;
		now->next->prev = now->prev;
	}
	now->prev = NULL;
	lru->head->prev = now;
	now->next = lru->head;
	lru->head = now;

	//lru_check_error(lru);
}

void lru_move_last(LRU *lru, lru_node* target){
	if(target == NULL){
		return ;
	}
	if(target == lru->tail){
		return ;
	}
	if(target == lru->head){
		lru->head = target->next;
		lru->head->prev = NULL;
	}
	else{
		target->prev->next = target->next;
		target->next->prev = target->prev;
	}

	target->prev = lru->tail;
	target->next = NULL;
	lru->tail->next = target;
	lru->tail = target;
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

	if(lru->retrieve_key){
		uint32_t key=lru->retrieve_key(now->data);
		art_delete(&lru->map, (const unsigned char*)&key, sizeof(key));
	}
	
	lru->size--;
	if(lru->free_data){
		lru->free_data(now->data);
	}
	//lru_check_error(lru);
	free(now);
}

void* lru_find(LRU *lru, uint32_t key){
	return art_search(&lru->map, (const unsigned char*)&key, sizeof(key));
}
