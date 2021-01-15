#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include "gc.h"
#include "mapping.h"
#include "utils.h"
#include "../../include/data_struct/list.h"

extern algorithm pftl;

gc_value* pftl_send_gc_req(uint32_t ppa, uint8_t type, value_set *value) {
	algo_req *my_req;
	gc_value *res;

	my_req = (algo_req *)malloc(sizeof(algo_req));
	res = (gc_value *)malloc(sizeof(gc_value));
	
	my_req->parents=NULL;
	my_req->end_req = pftl_gc_end_req;
	my_req->type=type;
	
	switch(type) {
		case GCDR:
			res->ppa = ppa;
			res->value = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			res->isdone = false;
			my_req->params = (void *)res;

			pftl.li->read(ppa, PAGESIZE, res->value, ASYNC, my_req);
			break;
		case GCDW:
			res->value = value;
			my_req->params = (void *)res;

			pftl.li->write(ppa, PAGESIZE, res->value, ASYNC, my_req);
			break;
	}

	return res;
}

void pftl_gc() {
	blockmanager *bm;
	__gsegment *target;
	pm_body *p;
	align_gc_buffer ali_gc_buf;
	gc_value *gv;
	value_set *value;
	list *temp_list;
	li_node *now, *nxt;
	KEYT *lbas;
	uint32_t page;
	uint32_t bidx, pidx;
	uint32_t res;
	bool read_valid;

	bm = pftl.bm;
	target = bm->get_gc_target(bm);
	p = (pm_body *)pftl.algo_body;
	temp_list = list_init();
	ali_gc_buf.idx = 0;

	for_each_page_in_seg(target, page, bidx, pidx) {
		read_valid = false;
		
		for(uint32_t i = 0; i < L2PGAP; i++) {
			if(!bm->is_invalid_page(bm, page * L2PGAP + i)) {
				read_valid = true;
				break;
			}
		}

		if(read_valid) {
			gv = pftl_send_gc_req(page, GCDR, NULL);
			list_insert(temp_list, (void *)gv);
		}
	}

	while(temp_list->size){
		for_each_list_node_safe(temp_list, now, nxt) {
			gv = (gc_value *)now->data;
			
			if(!gv->isdone) {
				continue;
			}

			lbas = (KEYT *)bm->get_oob(bm, gv->ppa);

			for(uint32_t i = 0; i < L2PGAP; i++) {
				if(gv->ppa * L2PGAP + i == 1933569) {
					printf("target is gcing %u\n", 1933569);
				}

				if(bm->is_invalid_page(bm, gv->ppa * L2PGAP + i)) {
					continue;
				}

				memcpy(&ali_gc_buf.value[ali_gc_buf.idx * 4096], &gv->value->value[i * 4096], 4096);
				ali_gc_buf.key[ali_gc_buf.idx] = lbas[i];
				ali_gc_buf.idx++;

				if(ali_gc_buf.idx == L2PGAP) {
					res = pftl_gc_update_mapping(ali_gc_buf.key, L2PGAP);
					value = inf_get_valueset(ali_gc_buf.value, FS_MALLOC_W, PAGESIZE);
					pftl_send_gc_req(res, GCDW, value);
					ali_gc_buf.idx = 0;
				}
			}

			inf_free_valueset(gv->value, FS_MALLOC_R);
			free(gv);
			list_delete_node(temp_list, now);
		}
	}

	if(ali_gc_buf.idx){
		res = pftl_gc_update_mapping(ali_gc_buf.key, ali_gc_buf.idx);
		value = inf_get_valueset(ali_gc_buf.value, FS_MALLOC_W, PAGESIZE);
		pftl_send_gc_req(res, GCDW, value);
	}

	bm->trim_segment(bm, target, pftl.li);	
	bm->free_segment(bm, p->active);

	p->active = p->reserve;
	p->reserve = bm->change_reserve(bm, p->reserve);

	list_free(temp_list);
}

void *pftl_gc_end_req(algo_req *input) {
	gc_value *gv;
	
	gv = (gc_value *)input->params;
	
	switch(input->type) {
		case GCDR:
			gv->isdone = true;
			break;
		case GCDW:
			inf_free_valueset(gv->value, FS_MALLOC_R);
			free(gv);
			break;
	}

	free(input);

	return NULL;
}
