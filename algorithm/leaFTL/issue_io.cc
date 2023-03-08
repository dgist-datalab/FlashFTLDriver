#include "issue_io.h"
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
    case GCDR:
        li->read(ppa, PAGESIZE, value, res);
        break;
    case GCDW:
    case DATAW:
        li->write(ppa, PAGESIZE, value, res);
        break;
    default:
        printf("not defined type %s:%u\n", __FUNCTION__, __LINE__);
        break;
    }
    return res;
}

algo_req *send_IO_user_req(uint32_t type, lower_info *li, uint32_t ppa, value_set *value, void *parameter, request *req, void *(*end_req)(algo_req * const)){
    algo_req *res=algo_req_init(req, type, parameter, end_req);
    switch (type)
    {
    case DATAR:
        li->read(ppa, PAGESIZE, value, res);
        break;
    default:
        printf("not defined type %s:%u\n", __FUNCTION__, __LINE__);
        break;
    }
    return res;
}