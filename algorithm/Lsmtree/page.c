#include "page.h"
#include "nocpy.h"
#include "../../include/sem_lock.h"
extern lsmtree LSM;
extern algorithm algo_lsm;
pm d_m;
pm map_m;
extern volatile int gc_target_get_cnt;
//block* getRBLOCK(uint8_t type);
volatile int gc_read_wait;
fdriver_lock_t gc_wait;
void gc_general_wait_init(){
	fdriver_lock(&gc_wait);
}
void gc_general_waiting(){
#ifdef MUTEXLOCK
		if(gc_read_wait!=0){
			fdriver_lock(&gc_wait);
		}
#elif defined(SPINLOCK)
		while(gc_target_get_cnt!=gc_read_wait){}
#endif
		fdriver_unlock(&gc_wait);
		gc_read_wait=0;
		gc_target_get_cnt=0;
}

void pm_init(){
	blockmanager *bm=LSM.bm;
	d_m.reserve=bm->pt_get_segment(bm,DATA_S,true);
	d_m.target=NULL;
	d_m.active=NULL;

	map_m.reserve=bm->pt_get_segment(bm,MAP_S,true);
	map_m.target=NULL;
	map_m.active=NULL;
	if(LSM.comp_opt==HW || LSM.comp_opt==MIXEDCOMP){
		q_init(&map_m.erased_q,_PPS);
	}
	
	fdriver_mutex_init(&gc_wait);
}

lsm_block* lb_init(uint8_t type){
	lsm_block *lb;
	lb=(lsm_block*)calloc(sizeof(lsm_block),1);
	lb->erased=true;

	if(type==DATA){
		lb->isdata_block=1;
#ifdef DVALUE
		lb->bitset=(uint8_t *)calloc(sizeof(uint8_t),_PPB*NPCINPAGE/8);
#endif
	}
	return lb;
}

void lb_free(lsm_block *b){
	if(b){
#ifdef DVALUE
		free(b->bitset);
#endif
		free(b);
	}
}

uint32_t getPPA(uint8_t type, KEYT lpa, bool b){
	pm *t;
	blockmanager *bm=LSM.bm;
	uint32_t res=-1;
	static int cnt=0;
retry:
	if(type==DATA){
		t=&d_m;
		if(!t->active || bm->check_full(bm,t->active,MASTER_PAGE)){
			if(bm->pt_isgc_needed(bm,DATA_S)){
				gc_data();
				goto retry;
			}
			t->active=bm->pt_get_segment(bm,DATA_S,false);
		}
	}else if(type==HEADER){
		t=&map_m;
		if(!t->active || bm->check_full(bm,t->active,MASTER_PAGE)){
			if(bm->pt_isgc_needed(bm,MAP_S)){
				bm->check_full(bm,t->active,MASTER_PAGE);
				gc_header();
				if(!t->reserve){
					change_new_reserve(MAP_S);
				}
				//abort();
				goto retry;
			}
			t->active=bm->pt_get_segment(bm,MAP_S,false);
		}
	}
	else{
		printf("fuck! no type getPPA");
		abort();
	}
	if(type==HEADER && (LSM.comp_opt==HW || LSM.comp_opt==MIXEDCOMP )&& map_m.erased_q->size ){
		void *t=q_dequeue(map_m.erased_q);
		res=*(uint32_t*)t;
		free(t);
	}
	else
		res=bm->get_page_num(bm,t->active);
	if(res==UINT_MAX){
		printf("cnt:%d\n",cnt);
		abort();
	}

//	printf("%d\n",res);
	validate_PPA(type,res);
	return res;
}

uint32_t getRPPA(uint8_t type,KEYT lpa, bool b,__gsegment *issame_bl){
	pm *t;
	blockmanager *bm=LSM.bm;
	uint32_t res=-1;
	if(type==DATA){
		t=&d_m;
	}else if(type==HEADER){
		t=&map_m;
	}
	else{
		printf("fuck! no type getPPA");
		abort();
	}
	
	if(type==HEADER){
		if(LSM.bm->check_full(LSM.bm,t->active,MASTER_BLOCK)){	
			if(issame_bl->blocks[0]->block_num==map_m.active->blocks[0]->block_num){
				printf("can't be\n");
				abort();
			}
			LSM.bm->pt_reserve_to_free(LSM.bm,MAP_S,t->reserve);
			t->reserve=NULL;
			t->active=bm->pt_get_segment(bm,MAP_S,false);
		//	printf("no tseg, reserve to active\n");
		}

		res=bm->get_page_num(bm,t->active);
	}
	validate_PPA(type,res);
	return res;
}

static int32_t block_age=INT32_MAX;
lsm_block* getBlock(uint8_t type){
	pm *t;
	blockmanager *bm=LSM.bm;

retry:
	if(type==DATA){
		t=&d_m;
		if(!t->active || bm->check_full(bm,t->active,MASTER_BLOCK)){
			if(bm->pt_isgc_needed(bm,DATA_S)){
				bm->check_full(bm,t->active,MASTER_BLOCK);
				if(LSM.gc_opt){
					LSM.lop->print_level_summary();
					abort();
				}
				gc_data();
				goto retry;
			}
			t->active=bm->pt_get_segment(bm,DATA_S,false);
		}
	}else if(type==HEADER){
		t=&map_m;
		if(!t->active || bm->check_full(bm,t->active,MASTER_BLOCK)){
			if(bm->pt_isgc_needed(bm,MAP_S)){
				gc_header();
				abort();
				goto retry;
			}
			t->active=bm->pt_get_segment(bm,DATA_S,false);
		}
	}
	else{
		printf("fuck! no type getPPA");
		abort();
	}

	//__block *res=bm->get_block(bm,t->active);
	ppa_t pick_ppa=bm->get_page_num(bm,t->active);
	__block *res=bm->pick_block(bm,pick_ppa);
	res->age=block_age--;
	lsm_block *lb;
	if(!res->private_data){
		lb=lb_init(type);
		res->private_data=(void*)lb;
	}else{
		lb=(lsm_block*)res->private_data;
	}

	lb->now_ppa=pick_ppa;
	return lb;
}
lsm_block* getRBlock(uint8_t type){
	pm *t;
	blockmanager *bm=LSM.bm;
	if(type==DATA){
		t=&d_m;
	}else if(type==HEADER){
		t=&map_m;
	}
	else{
		printf("fuck! no type getPPA");
		abort();
	}

	ppa_t pick_ppa=bm->get_page_num(bm,t->reserve);
	__block *res=bm->pick_block(bm,pick_ppa);
	res->age=block_age--;
	lsm_block *lb;
	if(!res->private_data){
		lb=lb_init(type);
		res->private_data=(void*)lb;
	}
	else{
		lb=(lsm_block*)res->private_data;
	}
	lb->now_ppa=pick_ppa;
	return lb;
}

bool invalidate_PPA(uint8_t type,uint32_t ppa){
	uint32_t t_p=ppa;
	void *t;
	switch(type){
		case DATA:
#ifdef DVALUE
			t_p=t_p/NPCINPAGE;
			if(ppa==UINT_MAX) return true;
			t=LSM.bm->pick_block(LSM.bm,t_p)->private_data;
			//invalidate_piece((lsm_block*)t,ppa);
#endif

#ifdef EMULATOR
			lsm_simul_del(ppa);
#endif
			break;
		case HEADER:
						break;
		default:
			printf("error in validate_ppa\n");
			abort();
	}

	int res=LSM.bm->unpopulate_bit(LSM.bm,t_p);
	if(type==HEADER){
		if(!res){
		//	LSM.lop->all_print();
			printf("target_ppa:%u\n",ppa);
			LSM.bm->unpopulate_bit(LSM.bm,t_p);
		//	LSM.lop->print(LSM.c_level);
		//	abort();
			return false;
		}
	}
	return true;
}

bool validate_PPA(uint8_t type, uint32_t ppa){
	uint32_t t_p=ppa;
	switch(type){
		case DATA:
#ifdef DVALUE
			t_p=t_p/NPCINPAGE;
			validate_piece((lsm_block*)LSM.bm->pick_block(LSM.bm,t_p)->private_data,ppa);

#endif
	
			break;
		case HEADER:
				break;
		default:
			printf("error in validate_ppa\n");
			abort();
	}

	
	int res=LSM.bm->populate_bit(LSM.bm,t_p);
	if(type==HEADER){
	//	printf("validate: %u\n",ppa);
		if(!res){
			abort();
			return false;
		}
	}
	return true;
}

void erase_PPA(uint8_t type,uint32_t ppa){
	uint32_t t_p=ppa;
	switch(type){
		case DATA:
#ifdef DVALUE
			t_p=t_p/NPCINPAGE;
			validate_piece((lsm_block*)LSM.bm->pick_block(LSM.bm,t_p)->private_data,ppa);
#endif
			break;
		case HEADER:
				break;
		default:
			printf("error in validate_ppa\n");
			abort();
	}

	int res=LSM.bm->erase_bit(LSM.bm,t_p);
	if(!res && type==HEADER){
		abort();
	}
	if(type==HEADER){
		uint32_t *temp=(uint32_t*)malloc(sizeof(uint32_t));
		*temp=ppa;
		q_enqueue((void*)temp,map_m.erased_q);
	}
}

void gc_data_write(uint64_t ppa,htable_t *value,uint8_t isdata){
	if(isdata!=GCMW && isdata!=GCMW_DGC && isdata!=GCDW){
		abort();
	}
	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));

	params->lsm_type=isdata;
	if(LSM.nocpy){
		params->value=inf_get_valueset((PTR)(value)->sets,FS_MALLOC_W,PAGESIZE);
		if(isdata==GCMW || isdata==GCMW_DGC){
			nocpy_copy_from_change((char*)value->nocpy_table,ppa);
		}
	}
	else
		params->value=inf_get_valueset((PTR)(value)->sets,FS_MALLOC_W,PAGESIZE);

	areq->parents=NULL;
	areq->end_req=lsm_end_req;
	areq->params=(void*)params;
	areq->type=params->lsm_type;
	areq->rapid=false;
	algo_lsm.li->write(isdata==GCDW?CONVPPA(ppa):ppa,PAGESIZE,params->value,ASYNC,areq);
	return;
}

void gc_data_read(uint64_t ppa,htable_t *value,uint8_t isdata, gc_node *t){
	if(isdata!=GCMR && isdata!=GCMR_DGC && isdata!=GCDR){
		abort();
	}
	gc_read_wait++;
	algo_req *areq=(algo_req*)malloc(sizeof(algo_req));
	lsm_params *params=(lsm_params*)malloc(sizeof(lsm_params));

	params->lsm_type=isdata;
	params->value=inf_get_valueset(NULL,FS_MALLOC_R,PAGESIZE);
	params->target=(PTR*)value->sets;
	params->ppa=ppa;
	
	value->origin=params->value;

	areq->parents=NULL;
	if(t){
		params->entry_ptr=(void*)t;
		areq->end_req=gc_data_end_req;
	}
	else{
		areq->end_req=lsm_end_req;
	}
	areq->params=(void*)params;
	areq->type_lower=0;
	areq->rapid=false;
	areq->type=params->lsm_type;
	if(LSM.nocpy &&  (isdata==GCMR || isdata==GCMR_DGC)){
		value->nocpy_table=nocpy_pick(ppa);
	}
	algo_lsm.li->read(isdata==GCDR?CONVPPA(ppa):ppa,PAGESIZE,params->value,ASYNC,areq);
	return;
}

bool gc_dynamic_checker(bool last_comp_flag){
	bool res=false;
	int test=LSM.bm->pt_remain_page(LSM.bm,	d_m.active, DATA_S);
	if((last_comp_flag && (uint32_t)test<LSM.needed_valid_page))
	{
		LSM.gc_started=true;
		res=true;
		LSM.target_gc_page=LSM.needed_valid_page;
	}

	/*
	int calc=LSM.needed_valid_page*LSM.check_cnt;
	calc+=LSM.last_level_comp_term;
	calc/=(LSM.check_cnt+1);*/
	LSM.needed_valid_page=LSM.last_level_comp_term;
	LSM.check_cnt++;
	//printf("%d %d(integ, now) - %d\n",LSM.needed_valid_page,LSM.last_level_comp_term, test);
	LSM.last_level_comp_term=0;
	return res;
}

void pm_set_oob(uint32_t _ppa, char *data, int len, int type){
	int ppa;
	if(type==DATA){
#ifdef DVALUE
		ppa=_ppa/NPCINPAGE;
#endif
	}else if(type==HEADER){
		ppa=_ppa;
	}
	else 
		abort();
	blockmanager *bm=LSM.bm;
	bm->set_oob(bm,data,len,ppa);
}

void *pm_get_oob(uint32_t _ppa, int type, bool isgc){
	//^ the third param is for debugging 
	int ppa;
	if(type==DATA){
#ifdef DVALUE
		ppa=_ppa;
#endif
	}else if(type==HEADER){
		ppa=_ppa;
	}
	else 
		abort();
	blockmanager *bm=LSM.bm;
	void *res=bm->get_oob(bm,ppa);

	if(!isgc){
		if(((footer*)res)->map[0]){
			printf("ppa:%u,%u %p\n",_ppa,ppa,res);
			abort();
		}
	}
	return res;
}

void gc_nocpy_delay_erase(uint32_t ppa){
	//if(ppa==UINT32_MAX) return;
	//nocpy_free_block(ppa);
	nocpy_trim_delay_flush();
	LSM.delayed_trim_ppa=UINT32_MAX;
}
void change_new_reserve(uint8_t type){
	pm *t=NULL;
	blockmanager *bm=LSM.bm;
	int pt_num=0;
	switch(type){
		case DATA:
			t=&d_m;
			pt_num=DATA_S;
			break;
		case HEADER:
			t=&map_m;
			pt_num=MAP_S;
			break;
	}
	t->reserve=bm->change_pt_reserve(bm,pt_num,t->reserve);
}

void change_reserve_to_active(uint8_t type){
	pm *t=NULL;
	blockmanager *bm=LSM.bm;
	int pt_num=0;
	switch(type){
		case DATA:
			t=&d_m;
			pt_num=DATA_S;
			break;
		case HEADER:
			t=&map_m;
			pt_num=MAP_S;
			break;
	}
	t->active=t->reserve;
	t->reserve=bm->change_pt_reserve(bm,pt_num,t->reserve);
}	
#ifdef DVALUE
void validate_piece(lsm_block *b, uint32_t ppa){
	uint32_t pc_idx=ppa%NPCINPAGE;
	ppa/=NPCINPAGE;
	uint32_t page=(ppa>>6)&0xff;
	uint32_t check_idx=page*NPCINPAGE+pc_idx;
	uint32_t bit_idx=check_idx/8;
	uint32_t bit_off=check_idx%8;
	/*
	if(b->bitset[bit_idx]&(1<<bit_off)){
		printf("ppa:%d\n",ppa);
		abort();
	}*/
	b->bitset[bit_idx]|=(1<<bit_off);
}

void invalidate_piece(lsm_block *b, uint32_t ppa){
	uint32_t pc_idx=ppa%NPCINPAGE;
	ppa/=NPCINPAGE;
	uint32_t page=(ppa>>6)&0xff;
	uint32_t check_idx=page*NPCINPAGE+pc_idx;
	uint32_t bit_idx=check_idx/8;
	uint32_t bit_off=check_idx%8;
	/*
	if(!(b->bitset[bit_idx]&(1<<bit_off))){
		printf("ppa:%u",ppa);
		abort();
	}*/
	b->bitset[bit_idx]&=~(1<<bit_off);
}

bool is_invalid_piece(lsm_block *b, uint32_t ppa){
	uint32_t pc_idx=ppa%NPCINPAGE;
	ppa/=NPCINPAGE;
	uint32_t page=(ppa>>6)&0xff;
	uint32_t check_idx=page*NPCINPAGE+pc_idx;
	uint32_t bit_idx=check_idx/8;
	uint32_t bit_off=check_idx%8;

	return !(b->bitset[bit_idx] & (1<<bit_off));
}

bool page_check_available(uint8_t type, uint32_t needed_page){
	pm *t;
	blockmanager *bm=LSM.bm;
	if(type==DATA){
		t=&d_m;	
	}
	else if(type==HEADER){
		t=&map_m;
	}
	else{
		abort();
	}
	if(type==HEADER && !t->active){
		t->active=bm->pt_get_segment(bm,MAP_S,false);
	}
	uint32_t res=bm->pt_remain_page(bm,t->active,MAP_S);
	static int cnt=0;
	if(cnt==1332 || cnt==1324){
		//printf("break!\n");
	}
	//printf("%d\n",cnt++);

	if(res<needed_page){
retry:
		int t_res;
		if(type==HEADER) t_res=gc_header();
		if(!t->reserve){
			change_new_reserve(MAP_S);
		}
		res=bm->pt_remain_page(bm,t->active,MAP_S);
		if(res<needed_page){
			goto retry;
		}
	}
	return true;
}
#endif
