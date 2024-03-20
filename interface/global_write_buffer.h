#pragma once
#include<map>
#include<stdlib.h>
#include<pthread.h>

#include "../include/types.h"
#include "../include/container.h"

class GlobalWriteBufferLock{
    public:
        GlobalWriteBufferLock(pthread_mutex_t *_mutex){
            this->mutex = _mutex;
            pthread_mutex_lock(this->mutex);
        }
        ~GlobalWriteBufferLock(){
            pthread_mutex_unlock(this->mutex);
        }
    private:
        pthread_mutex_t *mutex;
};

class GlobalWriteBuffer {
    public:
        GlobalWriteBuffer(int buffer_size);
        int write(request *req);
        request* read(request *req);
        int has_room(int add);
        bool isfull();
        ~GlobalWriteBuffer(){
            pthread_mutex_destroy(&this->mutex);
        }
        std::map<int, request *> buffer;
        pthread_mutex_t mutex;
    private:
        int buffer_size;
};

void init_global_write_buffer(int buffer_size);
void free_global_write_buffer();
int write_global_write_buffer(request *req);
int read_global_write_buffer(request *req);