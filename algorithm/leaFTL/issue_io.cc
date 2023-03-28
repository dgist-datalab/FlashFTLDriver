#include "issue_io.h"
#include "../../include/debug_utils.h"
#include "./leaFTL.h"
#include <pthread.h>

extern uint32_t lea_test_piece_ppa;
extern uint32_t read_buffer_hit_cnt;
extern page_read_buffer rb;

static inline algo_req *algo_req_init(request *req, uint32_t type, void *param, void *(*end_req)(algo_req *const)){
    algo_req *res=(algo_req*)malloc(sizeof(algo_req));
    res->parents=req;
    res->type=type;
    res->param=param;
    res->end_req=end_req;
    res->type_lower=0;
    return res;
}

algo_req *send_IO_back_req(uint32_t type, lower_info *li, uint32_t ppa, value_set *value, void *parameter, void *(*end_req)(algo_req * const)){
    algo_req *res=algo_req_init(NULL, type, parameter, end_req);
    switch (type){
    case GCMR:
    case GCDR:
    case MAPPINGR:
    case DATAR:
        li->read(ppa, PAGESIZE, value, res);
        break;
    case GCDW:
    case GCMW:
    case MAPPINGW:
    case DATAW:
        li->write(ppa, PAGESIZE, value, res);
        break;
        li->read(ppa, PAGESIZE, value, res);
        break;
    default:
        printf("not defined type %s:%u\n", __FUNCTION__, __LINE__);
        break;
    }
    return res;
}

algo_req *send_IO_user_req(uint32_t type, lower_info *li, uint32_t ppa, value_set *value, void *parameter, request *req, void *(*end_req)(algo_req * const)){
    algo_req *res=algo_req_init(req, type, parameter, end_req);
	rb_r_iter temp_r_iter;
    switch (type)
    {
    case DATAR:
	    fdriver_lock(&rb.read_buffer_lock);
	    if(ppa==rb.buffer_ppa){
	    	read_buffer_hit_cnt++;
	    	memcpy(value->value, rb.buffer_value, PAGESIZE);
	    	req->buffer_hit++;
	    	fdriver_unlock(&rb.read_buffer_lock);
	    	return res;
	    }
	    fdriver_unlock(&rb.read_buffer_lock);

		fdriver_lock(&rb.pending_lock);
		temp_r_iter=rb.issue_req->find(ppa);
		if(temp_r_iter==rb.issue_req->end()){
			rb.issue_req->insert(std::pair<uint32_t,algo_req*>(ppa, res));
			fdriver_unlock(&rb.pending_lock);
		}
		else{
			req->buffer_hit++;
			rb.pending_req->insert(std::pair<uint32_t, algo_req*>(ppa, res));
			fdriver_unlock(&rb.pending_lock);
			return NULL;
		}
    case MAPPINGR:
        li->read(ppa, PAGESIZE, value, res);
        break;
    default:
        printf("not defined type %u %s:%u\n",type, __FUNCTION__, __LINE__);
        break;
    }
    return NULL;
}


void *temp_write_end_req(algo_req *const req){
    req->parents->end_req(req->parents);
    free(req);
    return NULL;
}

algo_req *send_IO_temp_write(lower_info *li, request *req){
    static int cnt=0;
    algo_req *temp_algo_req;
    temp_algo_req=(algo_req*)malloc(sizeof(algo_req));
    temp_algo_req->end_req=temp_write_end_req;
    temp_algo_req->parents=req;
    uint32_t ppa=_NOP+(cnt++%(33554432-_NOP));
    li->write(ppa, PAGESIZE, req->value, temp_algo_req);
    return NULL;
}