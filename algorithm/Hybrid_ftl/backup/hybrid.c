#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include "hybrid.h"
#include "hybridmap.h"
#include "../../bench/bench.h"
#include "../../include/settings.h"

struct algorithm hybrid_ftl={
        .argument_set=hybrid_argument,
        .create=hybrid_create,
        .destroy=hybrid_destroy,
        .read=hybrid_read,
        .write=hybrid_write,
        .flush=hybrid_flush,
        .remove=hybrid_remove,
};

extern MeasureTime mt;
extern uint32_t test_key;

typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;
hybrid_read_buffer rb;
align_buffer a_buffer[_NOS]; //each block has buffer

uint32_t hybrid_create (lower_info* li,blockmanager *bm,algorithm *algo){
    algo->li=li; //lower_info means the NAND CHIP
    algo->bm=bm; //blockmanager is managing invalidation
    hybrid_map_create();

    rb.pending_req=new std::multimap<uint32_t, algo_req *>();
    rb.issue_req=new std::multimap<uint32_t, algo_req*>();
    fdriver_mutex_init(&rb.pending_lock);
    fdriver_mutex_init(&rb.read_buffer_lock);
    rb.buffer_ppa=UINT32_MAX;
    return 1;
}



uint32_t hybrid_flush(request * const req){
	abort();
}

inline void send_user_req(request *const req, uint32_t type, ppa_t ppa, value_set *value){
    if(type==DATAR){
        fdriver_lock(&rb.read_buffer_lock);
        if(ppa == rb.buffer_ppa){
            if(test_key==req->key){
                printf("%u page hit(piece_ppa:%u)\n", req->key,value->ppa);
            }
            memcpy(value->value, &rb.buffer_value[(value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
            req->type_ftl=req->type_lower=0;
            req->end_req(req);
            fdriver_unlock(&rb.read_buffer_lock);
            return;
        }
        fdriver_unlock(&rb.read_buffer_lock);
    }

    hybrid_param* param=(hybrid_param*)malloc(sizeof(hybrid_param));
    algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
    param->value=value;
    my_req->parents=req;//add the upper request
    my_req->end_req=hybrid_end_req;//this is callback function
    my_req->param=(void*)param;//add your parameter structure
    my_req->type=type;//DATAR means DATA reads, this affect traffics results
    /*you note that after read a PPA, the callback function called*/

    if(type==DATAR){
        fdriver_lock(&rb.pending_lock);
        rb_r_iter temp_r_iter=rb.issue_req->find(ppa);
        if(temp_r_iter==rb.issue_req->end()){
            rb.issue_req->insert(std::pair<uint32_t,algo_req*>(ppa, my_req));
            fdriver_unlock(&rb.pending_lock);
        }
        else{
            rb.pending_req->insert(std::pair<uint32_t, algo_req*>(ppa, my_req));
            fdriver_unlock(&rb.pending_lock);
            return;
        }
    }

    switch(type){
        case DATAR:
            hybrid_ftl.li->read(ppa,PAGESIZE,value,ASYNC,my_req);
            break;
        case DATAW:
            hybrid_ftl.li->write(ppa,PAGESIZE,value,ASYNC,my_req);
            break;
    }
}

uint32_t hybrid_read(request *const req){
    uint32_t lbn = req->key / (_PPS * L2PGAP) ;
    blockmanager * bm = hybrid_ftl.bm;
    for(uint32_t i=0; i<a_buffer[lbn].idx; i++){
        if(req->key==a_buffer[lbn].key[i]){
            printf("\nBuffered Hit!");
            memcpy(req->value->value, a_buffer[lbn].value[i]->value, LPAGESIZE);
            req->end_req(req);
            return 1;
        }
    }
    printf("\nRead key: %u",req->key);
    req->value->ppa=hybrid_map_pick(req->key);

    if(!bm->is_valid_page(bm,req->value->ppa)){
        req->type=FS_NOTFOUND_T;
        req->end_req(req);
    }
    else{
        send_user_req(req, DATAR, req->value->ppa/L2PGAP, req->value);
    }
    return 1;
}

uint32_t align_buffering(request *const req, KEYT key, value_set *value){
    bool overlap=false;
    uint32_t overlapped_idx=UINT32_MAX;
    uint32_t lbn = req->key / (_PPS*L2PGAP);
    //printf("\rseq number ----------  %u%", req->seq);
    for(uint32_t i=0; i<a_buffer[lbn].idx; i++){
        if(a_buffer[lbn].key[i]==req->key){
            overlapped_idx=i;
            overlap=true;
            break;
        }
    }

    uint32_t target_idx=overlap?overlapped_idx:a_buffer[lbn].idx;
    if(req){
        a_buffer[lbn].value[target_idx]=req->value;
        a_buffer[lbn].key[target_idx]=req->key;
    }
    else{ //for nothing
        a_buffer[lbn].value[target_idx]=value;
        a_buffer[lbn].key[target_idx]=key;
    }

    if(!overlap){ a_buffer[lbn].idx++;}

    if(a_buffer[lbn].idx==L2PGAP){
        ppa_t ppa = hybrid_map_assign(a_buffer[lbn].key, a_buffer[lbn].idx);

        value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
        for(uint32_t i=0; i<L2PGAP; i++){
            memcpy(&value->value[i*LPAGESIZE], a_buffer[lbn].value[i]->value, LPAGESIZE);
            inf_free_valueset(a_buffer[lbn].value[i], FS_MALLOC_W);
        }

        send_user_req(NULL, DATAW, ppa, value);
        a_buffer[lbn].idx=0;
    }
    return 1;
}

void hybrid_destroy (lower_info* li, algorithm *algo){
    delete rb.pending_req;
    delete rb.issue_req;
    return;
}

uint32_t hybrid_remove(request * const req){

}

uint32_t hybrid_write(request *const req){
    align_buffering(req, 0, NULL);
    req->value=NULL;
    req->end_req(req);
    return 0;
}


static void processing_pending_req(algo_req *req, value_set *v){
    request *parents=req->parents;
    hybrid_param *param=(hybrid_param*)req->param;
    memcpy(param->value->value, &v->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
    parents->type_ftl=parents->type_lower=0;
    parents->end_req(parents);
    free(param);
    free(req);
}

void *hybrid_end_req(algo_req* input){
    //this function is called when the device layer(lower_info) finish the request.
    rb_r_iter target_r_iter;
    rb_r_iter target_r_iter_temp;
    algo_req *pending_req;
    hybrid_param* param=(hybrid_param*)input->param;
    switch(input->type){
        case DATAW:
            inf_free_valueset(param->value,FS_MALLOC_W);
            break;
        case DATAR:
            fdriver_lock(&rb.pending_lock);
            target_r_iter=rb.pending_req->find(param->value->ppa/L2PGAP);
            for(;target_r_iter->first==param->value->ppa/L2PGAP &&
                 target_r_iter!=rb.pending_req->end();){
                pending_req=target_r_iter->second;
                processing_pending_req(pending_req, param->value);
                rb.pending_req->erase(target_r_iter++);
            }
            rb.issue_req->erase(param->value->ppa/L2PGAP);
            fdriver_unlock(&rb.pending_lock);

            fdriver_lock(&rb.read_buffer_lock);
            rb.buffer_ppa=param->value->ppa/L2PGAP;
            memcpy(rb.buffer_value, param->value->value, PAGESIZE);
            fdriver_unlock(&rb.read_buffer_lock);

            if(param->value->ppa%L2PGAP){
                memmove(param->value->value, &param->value->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
            }

            break;
    }
    request *res=input->parents;
    if(res){
        res->type_ftl=res->type_lower=0;
        res->end_req(res);//you should call the parents end_req like this
    }
    free(param);
    free(input);
    return NULL;
}

inline uint32_t xx_to_byte(char *a){
    switch(a[0]){
        case 'K':
            return 1024;
        case 'M':
            return 1024*1024;
        case 'G':
            return 1024*1024*1024;
        default:
            break;
    }
    return 1;
}

uint32_t hybrid_argument(int argc, char **argv){
    bool cache_size;
    uint32_t len;
    int c;
    char temp;
    uint32_t base;
    uint32_t value;
    while((c=getopt(argc,argv,"c"))!=-1){
        switch(c){
            case 'c':
                cache_size=true;
                len=strlen(argv[optind]);
                temp=argv[optind][len-1];
                if(temp < '0' || temp >'9'){
                    argv[optind][len-1]=0;
                    base=xx_to_byte(&temp);
                }
                value=atoi(argv[optind]);
                value*=base;
                break;
            default:
                break;
        }
    }
    return 1;
}


