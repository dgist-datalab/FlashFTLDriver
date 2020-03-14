#ifndef RWLOCK_HEADER
#define RWLOCK_HEADER
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct{
	pthread_mutex_t lock;
	pthread_mutex_t cnt_lock;
	int readcnt;
}rwlock;

void rwlock_init(rwlock *);
void rwlock_read_lock(rwlock*);
void rwlock_read_unlock(rwlock*);
void rwlock_write_lock(rwlock*);
void rwlock_write_unlock(rwlock*);
#endif
