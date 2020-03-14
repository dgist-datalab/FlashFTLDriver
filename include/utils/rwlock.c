#include "rwlock.h"
void rwlock_init(rwlock *rw){
	pthread_mutex_init(&rw->lock,NULL);
	pthread_mutex_init(&rw->cnt_lock,NULL);
	rw->readcnt=0;
}

void rwlock_read_lock(rwlock* rw){
	pthread_mutex_lock(&rw->cnt_lock);
	rw->readcnt++;
	if(rw->readcnt==1){
		pthread_mutex_lock(&rw->lock);
	}
	pthread_mutex_unlock(&rw->cnt_lock);
}

void rwlock_read_unlock(rwlock *rw){
	pthread_mutex_lock(&rw->cnt_lock);
	rw->readcnt--;
	if(rw->readcnt==0){
		pthread_mutex_unlock(&rw->lock);
	}
	pthread_mutex_unlock(&rw->cnt_lock);
}

void rwlock_write_lock(rwlock *rw){
	pthread_mutex_lock(&rw->lock);
}

void rwlock_write_unlock(rwlock *rw){
	pthread_mutex_unlock(&rw->lock);
}
