#include "./global_write_buffer.h"
#include "../include/utils/thpool.h"
#include "./threading.h"

extern master_processor mp;
static GlobalWriteBuffer *gwb[2];
bool gwb_index = 0;
threadpool flush_th;  

bool temp_end_req(request *req) {
    if(req->value){
        inf_free_valueset(req->value, FS_MALLOC_W);
    }
    free(req);
    return true;
}

GlobalWriteBuffer::GlobalWriteBuffer(int buffer_size) {
    this->buffer_size = buffer_size;
    this->buffer=std::map<int, request *>();
    pthread_mutex_init(&this->mutex, NULL);
}

int GlobalWriteBuffer::write(request *req){
    GlobalWriteBufferLock lock_(&this->mutex);
    if (this->buffer.size() >= this->buffer_size) {
        return 0;
    }
    std::map<int, request *>::iterator it=this->buffer.find(req->key);
    if(it==this->buffer.end()){
        this->buffer.insert(std::pair<int, request *>(req->key, req));
        return 1;
    }
    else{
        temp_end_req(it->second);
        it->second=req;
    }
    return 1;
}

request* GlobalWriteBuffer::read(request *req) {
    GlobalWriteBufferLock lock_(&this->mutex);
    std::map<int, request *>::iterator it;
    it = this->buffer.find(req->key);
    if (it != this->buffer.end()) {
        return it->second;
    }
    return NULL;
}

bool GlobalWriteBuffer::isfull() {
    GlobalWriteBufferLock lock_(&this->mutex);
    return this->buffer.size() == this->buffer_size;
}

int GlobalWriteBuffer::has_room(int add) {
    GlobalWriteBufferLock lock_(&this->mutex);
    return this->buffer.size() + add <= this->buffer_size;
}

void init_global_write_buffer(int buffer_size) {
    flush_th = thpool_init(1);
    gwb[0] = new GlobalWriteBuffer(buffer_size);
    gwb[1] = new GlobalWriteBuffer(buffer_size);
}


void free_global_write_buffer() {
    thpool_wait(flush_th);
    thpool_destroy(flush_th);

    for(int j=0; j< 2; j++){
        GlobalWriteBuffer *target=gwb[j];
        std::map<int, request *>::iterator it;
        for(it=target->buffer.begin(); it!=target->buffer.end();){
            temp_end_req(it->second);
            target->buffer.erase(it++);
        }    
    }


    delete gwb[0];
    delete gwb[1];
}

request *copy_org_req(request *input){
    request *res=(request *)malloc(sizeof(request));
    res->type=input->type;
    res->key=input->key;
    res->param=NULL;
    res->value=inf_get_valueset(input->value->value, FS_MALLOC_W, PAGESIZE);
    res->end_req=temp_end_req;
    res->tag_num=-1;
    res->global_seq=input->global_seq;
    //printf("gs:%u\n", res->global_seq);
    input->end_req(input);
    return res;
}

void flush_job(void *arg, int idx){
    GlobalWriteBuffer *target=(GlobalWriteBuffer *)arg;
    GlobalWriteBufferLock lock_(&target->mutex);
    std::map<int, request *>::iterator it;
    for(it=target->buffer.begin(); it!=target->buffer.end();){
        mp.algo->write(it->second);
        target->buffer.erase(it++);
    }
}

int write_global_write_buffer(request *req) {
#ifndef GLOBAL_WRITE_BUFFER
    return -1;
#endif
    if(req->param) return -1;

    gwb[gwb_index]->write(copy_org_req(req));

    if(gwb[gwb_index]->isfull()){
        thpool_wait(flush_th); //wait for flush
        thpool_add_work(flush_th, flush_job, gwb[gwb_index]);
        gwb_index = !gwb_index;
    }

    if(gwb[gwb_index]->has_room(1)) return 0;
    else return 1;
}

int read_global_write_buffer(request *req) {
#ifndef GLOBAL_WRITE_BUFFER
    return -1;
#endif
    if(req->param) return -1;
    request *res=gwb[!gwb_index]->read(req);
    if(res){
        memcpy(req->value->value, res->value->value, LPAGESIZE);
        req->end_req(req); 
        return 1;
    }

    res=gwb[gwb_index]->read(req);
    if(res){
        memcpy(req->value->value, res->value->value, LPAGESIZE);
        req->end_req(req); 
        return 1;
    }

    return -1;
}