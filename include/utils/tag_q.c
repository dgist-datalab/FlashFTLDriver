#include "tag_q.h"
#include <stdlib.h>
#include <stdio.h>

tag_manager *tag_manager_init(uint32_t tag_num){
	tag_manager *tmanager=(tag_manager*)malloc(sizeof(tag_manager));
	pthread_mutex_init(&tmanager->tag_lock, NULL);
	pthread_cond_init(&tmanager->tag_cond, NULL);
	tmanager->tagQ=new std::queue<uint32_t>();

	for(uint32_t i=0; i<tag_num; i++){
		tmanager->tagQ->push(i);
	}
	tmanager->max_tag_num=tag_num;
	return tmanager;
}

uint32_t tag_manager_get_tag(tag_manager *tm){
	uint32_t res;
	pthread_mutex_lock(&tm->tag_lock);
	while(tm->tagQ->empty()){
		pthread_cond_wait(&tm->tag_cond, &tm->tag_lock);
	}
	res=tm->tagQ->front();
	tm->tagQ->pop();
	pthread_mutex_unlock(&tm->tag_lock);
	//printf("get tag %u\n", res);
	return res;
}

void tag_manager_free_tag(tag_manager *tm, uint32_t tag_num){
	//printf("free tag %u\n", tag_num);
	pthread_mutex_lock(&tm->tag_lock);
	tm->tagQ->push(tag_num);
	if(tm->max_tag_num < tm->tagQ->size()){
		printf("over free in tagQ\n");
		abort();
	}
	pthread_cond_broadcast(&tm->tag_cond);
	pthread_mutex_unlock(&tm->tag_lock);
}

void tag_manager_free_manager(tag_manager *tm){
	pthread_mutex_destroy(&tm->tag_lock);
	delete tm->tagQ;
	free(tm);
}
