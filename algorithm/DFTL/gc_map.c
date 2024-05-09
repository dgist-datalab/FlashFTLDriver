#include "gc.h"
#include "demand_mapping.h"
#include <queue>
extern struct algorithm demand_ftl;
extern uint32_t test_ppa;
extern uint32_t test_key;
extern demand_map_manager dmm;


extern bool limit_map_seg;
extern uint32_t now_map_seg;
extern uint32_t now_data_seg;
extern uint32_t map_seg_limit;
extern uint32_t data_seg_limit;

void invalidate_map_ppa(uint32_t piece_ppa){
#ifdef DFTL_DEBUG
	printf("map invalidate :%u\n", piece_ppa);
#endif
	for(uint32_t i=0; i<L2PGAP; i++){
		if(!demand_ftl.bm->bit_unset(demand_ftl.bm, piece_ppa+i)){
			EPRINT("double invalidation!", true);
		}
		dmm.li->invalidate_inform(piece_ppa/L2PGAP);
	}
}

void validate_map_ppa(uint32_t piece_ppa, KEYT gtd_idx){
	for(uint32_t i=0; i<L2PGAP; i++){
		if(!demand_ftl.bm->bit_set(demand_ftl.bm, piece_ppa+i)){
			EPRINT("double validation!", true);
		}
	}
	demand_ftl.bm->set_oob(demand_ftl.bm,(char*)&gtd_idx, sizeof(KEYT), piece_ppa/L2PGAP);
}

ppa_t get_map_ppa(KEYT gtd_idx, bool *gc_triggered){
	uint32_t res;
	pm_body *p=(pm_body*)demand_ftl.algo_body;
	if(demand_ftl.bm->check_full(p->map_active)){
		if((limit_map_seg && now_map_seg > map_seg_limit) || demand_ftl.bm->is_gc_needed(demand_ftl.bm)){
			do_map_gc();//call gc
			if(gc_triggered){
				*gc_triggered=true;
			}
		}
	}

retry:
	res=demand_ftl.bm->get_page_addr(p->map_active);
	if(res==UINT32_MAX){
		p->map_active=demand_ftl.bm->get_segment(demand_ftl.bm, BLOCK_ACTIVE); //get a new block
		//p->map_active=demand_ftl.bm->get_segment(demand_ftl.bm, false); //get a new block
		p->seg_type_checker[p->map_active->seg_idx]=MAPSEG;
		now_map_seg++;
		goto retry;
	}
	/*
	if(GETGTDIDX(test_key)==gtd_idx){
		printf("%u mapping change to %u\n", test_key, res*L2PGAP);
	}*/

	validate_map_ppa(res*L2PGAP, gtd_idx);
	return res;
}

ppa_t get_map_rppa(KEYT gtd_idx){
	uint32_t res=0;
	pm_body *p=(pm_body*)demand_ftl.algo_body;

	/*when the gc phase, It should get a page from the reserved block*/
	res=demand_ftl.bm->get_page_addr(p->map_reserve);
	KEYT temp[L2PGAP]={gtd_idx, 0};
	validate_ppa(res, temp, L2PGAP);
	return res;
}
extern demand_map_manager dmm;

void do_map_gc(){

	static int cnt=0;
	pm_body *p=(pm_body*)demand_ftl.algo_body;
	blockmanager *bm=demand_ftl.bm;
	printf("map gc:%u\n", ++cnt);
	__gsegment *target=NULL;
	std::queue<uint32_t> temp_queue;
	while(!target || 
			p->seg_type_checker[target->seg_idx]!=MAPSEG){
		if(target){
			if(p->seg_type_checker[target->seg_idx]==DATASEG && target->all_invalid){
				break;	
			}
			temp_queue.push(target->seg_idx);
			free(target);
		}
		target=bm->get_gc_target(bm);
	} 
/*
	segment_print(false);*/
	//printf("map_gc target seg_idx:%u\n", target->seg_idx);

	uint32_t seg_idx;
	while(temp_queue.size()){
		seg_idx=temp_queue.front();
		bm->insert_gc_target(bm, seg_idx);
		temp_queue.pop();
	}

	//static int cnt=0;
	//printf("map_gc:%u seg_idx:%u (piece_ppa:%u~%u)\n", cnt++, target->seg_idx, target->seg_idx*L2PGAP*_PPS, (target->seg_idx+1)*L2PGAP*_PPS-1);

	list *temp_list=NULL;	
	gc_value *gv;
	uint32_t page;
	uint32_t bidx, pidx;
	if((p->seg_type_checker[target->seg_idx]==DATASEG && target->all_invalid) ||
			(p->seg_type_checker[target->seg_idx]==MAPSEG && target->all_invalid)){
		goto finish;
	}

	temp_list=list_init();
	for_each_page_in_seg(target,page,bidx,pidx){
		//this function check the page is valid or not
		bool should_read=false;
		for(uint32_t i=0; i<L2PGAP; i++){
			if(bm->is_invalid_piece(bm,page*L2PGAP)) continue;
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

			uint32_t old_ppa=dmm.GTD[gtd_idx[0]].physical_address;
			invalidate_map_ppa(old_ppa);
			
			new_ppa=get_map_rppa(gtd_idx[0]);
			dmm.GTD[gtd_idx[0]].physical_address=new_ppa*L2PGAP;
			send_req(new_ppa, GCMW, NULL, gv);
			list_delete_node(temp_list,now);
		}
	}

finish:

	if(p->seg_type_checker[target->seg_idx]==MAPSEG){
		now_map_seg--;
	}
	else{
		now_data_seg--;
	}

	bm->trim_segment(bm,target); //erase a block
	p->map_active=p->map_reserve;//make reserved to active block
	bm->change_reserve_to_active(bm, p->map_reserve);
	p->map_reserve=bm->get_segment(bm, BLOCK_RESERVE);//get new reserve block from block_manager
	now_map_seg++;
	p->seg_type_checker[p->map_reserve->seg_idx]=MAPSEG;
	if(temp_list){
		list_free(temp_list);
	}
}
