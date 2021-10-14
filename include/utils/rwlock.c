#include "rwlock.h"
static int32_t cnt;
extern char rw_debug_flag;
void rwlock_init(rwlock *rw){
	//pthread_mutex_init(&rw->lock,NULL);
	//pthread_mutex_init(&rw->cnt_lock,NULL);
	fdriver_mutex_init(&rw->lock);
	fdriver_mutex_init(&rw->cnt_lock);
	rw->readcnt=0;
}

void rwlock_read_lock(rwlock* rw){
	fdriver_lock(&rw->cnt_lock);
	rw->readcnt++;
	/*
	if(rw_debug_flag){
			printf("rw->read_cnt:%u\n", rw->readcnt);
	}*/
	if(rw->readcnt==1){
		fdriver_lock(&rw->lock);
	}
	fdriver_unlock(&rw->cnt_lock);
}

uint32_t rwlock_try_write_lock(rwlock *rw){
	 if(fdriver_try_lock(&rw->lock)){
		 //not get
		 fdriver_lock(&rw->cnt_lock);
		 uint32_t read_cnt=rw->readcnt;
		 fdriver_unlock(&rw->cnt_lock);
		 if(read_cnt){
			 printf("read wait\n");
			 //wait
			fdriver_lock(&rw->lock);
			return 0;
		 }
		 else{
			return UINT32_MAX;
		 }
	 }
	 else{
		 //get
		return 0;
	 }
}

void rwlock_read_unlock(rwlock *rw){
	fdriver_lock(&rw->cnt_lock);
	rw->readcnt--;
	/*
	if(rw_debug_flag){
			printf("rw->read_cnt:%u\n", rw->readcnt);
	}*/
	if(rw->readcnt<0){
		printf("????\n");
		abort();
	}
	if(rw->readcnt==0){
		fdriver_unlock(&rw->lock);
	}
	fdriver_unlock(&rw->cnt_lock);
}

void rwlock_write_lock(rwlock *rw){
	fdriver_lock(&rw->lock);
}

void rwlock_write_unlock(rwlock *rw){
	fdriver_unlock(&rw->lock);
}


void rwlock_destroy(rwlock* rw){
	fdriver_destroy(&rw->lock);
	fdriver_destroy(&rw->cnt_lock);
}
