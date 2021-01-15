#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include "pftl.h"
#include "mapping.h"
#include "../../bench/bench.h"

align_buffer ali_buf;
extern MeasureTime mt;

struct algorithm pftl = {
	.argument_set=NULL,
	.create=pftl_create,
	.destroy=pftl_destroy,
	.read=pftl_read,
	.write=pftl_write,
	.flush=pftl_flush,
	.remove=pftl_remove,
};

inline void pftl_send_user_req(request *const req, uint32_t type, ppa_t ppa, value_set *value) {
	pftl_params *params;
    algo_req *my_req;

    params = (pftl_params *)malloc(sizeof(pftl_params));
	my_req = (algo_req *)malloc(sizeof(algo_req));
	
    params->value = value;
	my_req->parents = req;
	my_req->end_req = pftl_end_req;
	my_req->params = (void *)params; 
	my_req->type = type;

	switch(type) {
		case DATAR:
			pftl.li->read(ppa, PAGESIZE, value, ASYNC, my_req);
			break;
		case DATAW:
			pftl.li->write(ppa, PAGESIZE, value, ASYNC, my_req);
			break;
	}
}

uint32_t align_buffering(request *const req) {
    ppa_t ppa;
    uint32_t lba;
    value_set *value;

    lba = req->key;
    value = req->value;

    if(ali_buf.idx && ali_buf.key[0] == lba) {
		inf_free_valueset(ali_buf.value[0], FS_MALLOC_W);
        ali_buf.value[0] = value;
        ali_buf.key[0] = lba;
    }
    else {
        ali_buf.value[ali_buf.idx] = value;
        ali_buf.key[ali_buf.idx] = lba;
        ali_buf.idx++;
    }

	if(ali_buf.idx == L2PGAP) {
		ppa = pftl_assign_ppa(ali_buf.key);
		value = inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		
        for(uint32_t i = 0; i < L2PGAP; i++) {
			memcpy(&value->value[i * 4096], ali_buf.value[i]->value, 4096);
			inf_free_valueset(ali_buf.value[i], FS_MALLOC_W);
		}

		pftl_send_user_req(NULL, DATAW, ppa, value);
		ali_buf.idx = 0;
	}

	return 1;
}

uint32_t pftl_create(lower_info* li, blockmanager *bm, algorithm *algo) {
	algo->li=li;
	algo->bm=bm;

	pftl_mapping_create();

	return 1;
}

void pftl_destroy(lower_info* li, algorithm *algo) {
	pftl_mapping_free();

	return ;
}

uint32_t pftl_read(request *const req) {
    ppa_t ppa;
    uint32_t key;
    
    key = req->key;

    if(ali_buf.idx && ali_buf.key[0] == key) {
        memcpy(req->value, ali_buf.value[0], 4096);
        req->end_req(req);
        return 1;
    }

    ppa = pftl_get_mapped_ppa(key);
    req->value->ppa = ppa;

    if(ppa == UINT32_MAX) {
        req->type = FS_NOTFOUND_T;
        req->end_req(req);
    }
    else {
        pftl_send_user_req(req, DATAR, ppa / L2PGAP, req->value);
    }

    return 1;
}

uint32_t pftl_write(request *const req) {
    align_buffering(req);
	
    req->value=NULL;
	req->end_req(req);

	return 0;
}

uint32_t pftl_remove(request *const req) {
	for(uint8_t i = 0; i < ali_buf.idx; i++) {
		if(ali_buf.key[i] == req->key) {
			inf_free_valueset(ali_buf.value[i], FS_MALLOC_W);
			ali_buf.idx--;
		}
	}
	
	pftl_map_trim(req->key);
	req->end_req(req);

	return 0;
}

uint32_t pftl_flush(request *const req) {
	abort();
	req->end_req(req);
	return 0;
}

void *pftl_end_req(algo_req* input) {
	pftl_params *params;
	request *res;
    
    params = (pftl_params *)input->params;
    res = input->parents;

	switch(input->type) {
		case DATAW:
			inf_free_valueset(params->value, FS_MALLOC_W);
			break;
		case DATAR:
			if(params->value->ppa % L2PGAP) {
				memmove(params->value->value, &params->value->value[4096], 4096);
			}
			break;
	}
	
    if(res){
		res->type_ftl = res->type_lower = 0;
		res->end_req(res);
	}

	free(params);
	free(input);
	
    return NULL;
}

