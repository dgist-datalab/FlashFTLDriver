#include "gc.h"
#include "demand_mapping.h"
extern struct algorithm demand_ftl;
extern uint32_t test_ppa;
void invalidate_map_ppa(uint32_t piece_ppa){
	if(!demand_ftl.bm->unpopulate_bit(demand_ftl.bm, piece_ppa)){
		EPRINT("double invalidation!", true);
	}
}

void validate_map_ppa(uint32_t piece_ppa, KEYT gtd_idx){
	if(!demand_ftl.bm->populate_bit(demand_ftl.bm, piece_ppa)){
		EPRINT("double validation!", true);
	}
	demand_ftl.bm->set_oob(demand_ftl.bm,(char*)&gtd_idx, sizeof(KEYT), piece_ppa/L2PGAP);
}

ppa_t get_map_ppa(KEYT gtd_idx){
	uint32_t res;
	pm_body *p=(pm_body*)demand_ftl.algo_body;
	if(demand_ftl.bm->check_full(demand_ftl.bm, p->map_active,MASTER_PAGE) && demand_ftl.bm->pt_isgc_needed(demand_ftl.bm, MAP_S)){
		do_map_gc();//call gc
	}

retry:
	res=demand_ftl.bm->get_page_num(demand_ftl.bm, p->map_active);
	if(res==UINT32_MAX){
		demand_ftl.bm->free_segment(demand_ftl.bm, p->map_active);
		p->map_active=demand_ftl.bm->pt_get_segment(demand_ftl.bm,MAP_S, false); //get a new block
		goto retry;
	}

	validate_map_ppa(res*L2PGAP, gtd_idx);
	return res;
}

ppa_t get_map_rppa(KEYT gtd_idx){
	uint32_t res=0;
	pm_body *p=(pm_body*)demand_ftl.algo_body;

	/*when the gc phase, It should get a page from the reserved block*/
	res=demand_ftl.bm->get_page_num(demand_ftl.bm,p->map_reserve);
	KEYT temp[L2PGAP]={gtd_idx, 0};
	validate_ppa(res, temp);
	return res;
}
extern demand_map_manager dmm;

void do_map_gc(){
	//static int cnt=0;
	//printf("map gc:%u\n", ++cnt);
	__gsegment *target=demand_ftl.bm->pt_get_gc_target(demand_ftl.bm, MAP_S);

	blockmanager *bm=demand_ftl.bm;
	pm_body *p=(pm_body*)demand_ftl.algo_body;
	list *temp_list=NULL;	
	gc_value *gv;
	uint32_t page;
	uint32_t bidx, pidx;
	if(target->invalidate_number==_PPS){
		goto finish;
	}

	temp_list=list_init();
	for_each_page_in_seg(target,page,bidx,pidx){
		//this function check the page is valid or not
		bool should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_page(bm,page*L2PGAP)) continue;
			else{
				should_read=true;
				break;
			}
		}
		if(should_read){
			gv=send_req(page,GCMR,NULL, NULL);
			list_insert(temp_list,(void*)gv);
		}
	}

	li_node *now,*nxt;
	KEYT* gtd_idx;
	ppa_t new_ppa;
	while(temp_list->size){
		for_each_list_node_safe(temp_list,now,nxt){
			gv=(gc_value*)now->data;
			if(!gv->isdone) continue;
			gtd_idx=(KEYT*)bm->get_oob(bm, gv->ppa);
			new_ppa=get_map_rppa(gtd_idx[0]);
			dmm.GTD[gtd_idx[0]].physical_address=new_ppa*L2PGAP;
			send_req(new_ppa, GCMW, NULL, gv);
			list_delete_node(temp_list,now);
		}
	}

finish:
	bm->pt_trim_segment(bm,MAP_S,target,demand_ftl.li); //erase a block
	bm->free_segment(bm, p->map_active);

	p->map_active=p->map_reserve;//make reserved to active block
	p->map_reserve=bm->change_pt_reserve(bm, MAP_S, p->map_reserve); //get new reserve block from block_manager
	if(temp_list){
		list_free(temp_list);
	}
}
