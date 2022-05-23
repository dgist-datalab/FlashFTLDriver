#include "./rmw_checker.h"
static _rmw_checker rmw_checker;
void rmw_node_init(){
    fdriver_mutex_init(&rmw_checker.lock);
}

void rmw_node_insert(uint32_t lba, uint32_t offset, uint32_t length, uint32_t global_seq, char *value){
    fdriver_lock(&rmw_checker.lock);
    rmw_node *t_value=(rmw_node*)malloc(sizeof(rmw_node));
    t_value->lba=lba;
    t_value->global_seq=global_seq;

    t_value->offset=offset;
    t_value->length=length;
    t_value->value=value;
    fdriver_mutex_init(&t_value->lock);
    fdriver_lock(&t_value->lock);

    rmw_checker.rmw_set.insert(std::pair<uint32_t, rmw_node*>(lba, t_value));
    fdriver_unlock(&rmw_checker.lock);
}

bool rmw_node_merge(uint32_t lba, uint32_t global_seq, char *target_value){
    fdriver_lock(&rmw_checker.lock);
    bool res=false;
    std::multimap<uint32_t, rmw_node*>::iterator iter=rmw_checker.rmw_set.find(lba);
    if(iter==rmw_checker.rmw_set.end()) {
        fdriver_unlock(&rmw_checker.lock);
        return false;
    }

    res=true;
    std::map<uint32_t, rmw_node*> temp_set;//global_seq, rmw_node    
    for(;iter->first==lba && iter!=rmw_checker.rmw_set.end(); iter++){
        if(iter->second->global_seq < global_seq){
            temp_set.insert(std::pair<uint32_t, rmw_node*>(iter->second->global_seq, iter->second));
        }
        else{
            break;
        }
    }

    std::map<uint32_t, rmw_node*>::iterator iter2=temp_set.begin();
    for(;iter2!=temp_set.end(); iter2++){
        uint32_t offset=iter2->second->offset;
        uint32_t length=iter2->second->length;

        fdriver_lock(&iter2->second->lock);
        memcpy(&target_value[offset*LPAGESIZE], &iter2->second->value[offset*LPAGESIZE], length*LPAGESIZE);
    }
    fdriver_unlock(&rmw_checker.lock);
    return res;
}

bool rmw_check(uint32_t lba){
    fdriver_lock(&rmw_checker.lock);
    std::multimap<uint32_t, rmw_node*>::iterator iter=rmw_checker.rmw_set.find(lba);
    if(iter==rmw_checker.rmw_set.end()) {
        fdriver_unlock(&rmw_checker.lock);
        return false;
    }
    else{
        fdriver_unlock(&rmw_checker.lock);
        return true;
    }
}

uint32_t rmw_node_pick(uint32_t lba, uint32_t global_seq, char *target_value){
    fdriver_lock(&rmw_checker.lock);
    uint32_t res=false;
    std::multimap<uint32_t, rmw_node*>::iterator iter=rmw_checker.rmw_set.find(lba);
    if(iter==rmw_checker.rmw_set.end()) {
        fdriver_unlock(&rmw_checker.lock);
        return 0;
    }

    res=true;
    std::map<uint32_t, rmw_node*> temp_set;//global_seq, rmw_node    
    for(;iter->first==lba && iter!=rmw_checker.rmw_set.end(); iter++){
        if(iter->second->global_seq < global_seq){
            temp_set.insert(std::pair<uint32_t, rmw_node*>(iter->second->global_seq, iter->second));
        }
        else{
            break;
        }
    }

    std::map<uint32_t, rmw_node*>::iterator iter2=temp_set.begin();
    for(;iter2!=temp_set.end(); iter2++){
        uint32_t offset=iter2->second->offset;
        uint32_t length=iter2->second->length;

        uint32_t temp_res=(1<<(offset+length)-1);
        if(offset!=0){
            temp_res^=((1<<offset)-1);
        }
        res|=temp_res;

        memcpy(&target_value[offset*LPAGESIZE], &iter2->second->value[offset*LPAGESIZE], length*LPAGESIZE);
    }
    fdriver_unlock(&rmw_checker.lock);
    return res;
}

void rmw_node_read_done(uint32_t lba, uint32_t global_seq){
    std::multimap<uint32_t, rmw_node*>::iterator iter=rmw_checker.rmw_set.find(lba);
    if(iter==rmw_checker.rmw_set.end()) return;
     for(;iter->first==lba && iter!=rmw_checker.rmw_set.end(); iter++){
        if(iter->second->global_seq==global_seq){
            fdriver_unlock(&iter->second->lock);
            return;
        }
    }
}

void rmw_node_delete(uint32_t lba, uint32_t global_seq){
    fdriver_lock(&rmw_checker.lock);
    std::multimap<uint32_t, rmw_node*>::iterator iter=rmw_checker.rmw_set.find(lba);
    if(iter==rmw_checker.rmw_set.end()){
        fdriver_unlock(&rmw_checker.lock);
        return;
    }

     for(;iter->first==lba && iter!=rmw_checker.rmw_set.end(); iter++){
        if(iter->second->global_seq==global_seq){
            fdriver_destroy(&iter->second->lock);
            free(iter->second);
            rmw_checker.rmw_set.erase(iter);
            fdriver_unlock(&rmw_checker.lock);
            return;
        }
    }
    fdriver_unlock(&rmw_checker.lock);
}

void rmw_node_free(){
    fdriver_destroy(&rmw_checker.lock);
}