#include "./gc.h"
#include "./piece_ppa.h"
#include "../../include/data_struct/list.h"

extern lower_info *g_li;
static void *__gc_end_req(algo_req *req){
	gc_value *g_value=(gc_value*)req->param;
	switch(req->type){
		case GCMR:
			fdriver_unlock(&g_value->lock);
			break;
		case GCMW:
			free(g_value);
			break;
		case GCDR:
			break;
		case GCDW:
			break;
	}
	free(req);
	return NULL;
}

static algo_req *__get_algo_req(gc_value *g_value, uint32_t type){
	algo_req *res=(algo_req*)malloc(sizeof(algo_req));
	res->type=type;
	res->ppa=g_value->ppa;
	res->value=g_value->value;
	res->end_req=__gc_end_req;
	res->param=(void*)g_value;
	return res;
}

static void __gc_issue_read(gc_value *g_value, blockmanager *sm, uint32_t type){
	algo_req *res=__get_algo_req(g_value, type);
	memcpy(g_value->oob, sm->get_oob(sm, res->ppa), sizeof(uint32_t) * L2PGAP);
	g_li->read(res->ppa, PAGESIZE, res->value, res);
}

static void __gc_issue_write(gc_value *g_value, blockmanager *sm, uint32_t type){
	algo_req *res=__get_algo_req(g_value, type);
	sm->set_oob(sm, (char*)g_value->oob, sizeof(uint32_t) *L2PGAP, res->ppa);
	g_li->write(res->ppa, PAGESIZE, res->value, res);
}

void gc_summary_segment(L2P_bm *bm, __gsegment *target){
	blockmanager *sm=bm->segment_manager;
	list *temp_list=list_init();
	uint32_t page, bidx, pidx;
	gc_value *g_value;
	for_each_page_in_seg(target,page,bidx,pidx){
		if(sm->is_invalid_piece(sm, page * L2PGAP)) continue;
		g_value=(gc_value*)malloc(sizeof(gc_value));
		g_value->ppa=page;
		g_value->value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		fdriver_mutex_init(&g_value->lock);
		fdriver_lock(&g_value->lock);

		__gc_issue_read(g_value, sm, GCMR);
		list_insert(temp_list, (void*)g_value);
	}

	li_node *now,*nxt;
	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){
			g_value=(gc_value*)now->data;
			fdriver_lock(&g_value->lock);
			fdriver_destroy(&g_value->lock);

			uint32_t sid=g_value->oob[0];
			uint32_t intra_idx=g_value->oob[1];

			sid_info info=sorted_array_master_get_info(sid);
			if(info.sa->sp_meta[intra_idx].ppa!=g_value->ppa){
				EPRINT("mapping error", true);
			}

			if(invalidate_ppa(sm, g_value->ppa, true) ==BIT_ERROR){
				EPRINT("invalidate map error", true);
			}
			uint32_t rppa=L2PBm_get_map_rppa(bm);
			if(validate_ppa(sm, rppa, true)==BIT_ERROR){
				EPRINT("validate map error", true);
			}
			g_value->ppa=rppa;

			__gc_issue_write(g_value, sm, GCMW);
			list_delete_node(temp_list,now);
		}
	}

	sm->trim_segment(sm, target);
	bm->now_summary_seg=bm->reserve_summary_seg;
	sm->change_reserve_to_active(sm, bm->reserve_summary_seg);
	bm->reserve_summary_seg=sm->get_segment(sm, BLOCK_RESERVE);
	list_free(temp_list);
}
