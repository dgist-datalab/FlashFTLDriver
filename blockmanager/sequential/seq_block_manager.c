#include "./seq_block_manager.h"
#include "../../include/debug_utils.h"
typedef struct blockmanager blockmanager;
extern blockmanager_master BMM;
#define INIT_INVALID_BLOCK_NUM (BPS+1)

static inline void __mh_swap_hptr(void *a, void *b){
	__segment *aa=(__segment*)a;
	__segment *bb=(__segment*)b;

	void *temp=aa->private_data;
	aa->private_data=bb->private_data;
	bb->private_data=temp;
}

static inline void  __mh_assign_hptr(void *a, void *hn){
	__segment *aa=(__segment*)a;
	aa->private_data=hn;
}

static inline int __mh_get_cnt(void *a){
	__segment *aa=(__segment*)a;
#ifdef layeredLSM
	return aa->invalid_block_num*_PPS*L2PGAP+aa->invalidate_piece_num;
#else
	return aa->invalidate_piece_num;
#endif
}

static inline void __set_function(blockmanager *bm){
	bm->get_segment=sbm_get_seg;
	bm->pick_segment=sbm_pick_seg;
	bm->get_page_addr=sbm_get_page_addr;
	bm->pick_page_addr=sbm_pick_page_addr;
	bm->check_full=default_check_full;
	bm->is_gc_needed=sbm_is_gc_needed;
	bm->get_gc_target=sbm_get_gc_target;
	bm->trim_segment=sbm_trim_segment;
	bm->bit_set=sbm_bit_set;
	bm->bit_unset=sbm_bit_unset;
	bm->bit_query=sbm_bit_query;
	bm->is_invalid_piece=sbm_is_invalid_piece;
	bm->set_oob=sbm_set_oob;
	bm->get_oob=sbm_get_oob;
	bm->change_reserve_to_active=sbm_change_reserve_to_active;
	bm->insert_gc_target=sbm_insert_gc_target;
	bm->total_free_page_num=sbm_total_free_page_num;
	bm->seg_invalidate_piece_num=sbm_seg_invalidate_piece_num;
	bm->invalidate_seg_num=sbm_invalidate_seg_num;
	bm->load=sbm_load;
	bm->dump=sbm_dump;
}

blockmanager*  sbm_create (lower_info *li){
	blockmanager *res=(blockmanager*)malloc(sizeof(blockmanager));
	res->type=SEQ_BM;
	res->li=li;
	__set_function(res);

	sbm_pri *pri=(sbm_pri*)calloc(1, sizeof(sbm_pri));
	mh_init(&pri->max_heap, _NOS, __mh_swap_hptr, __mh_assign_hptr, __mh_get_cnt);
	q_init(&pri->free_segment_q, _NOS);

	__segment *target;
	for(uint32_t i=0; i<_NOS; i++){
		target=&pri->seg_set[i];
		target->seg_idx=i;
		target->invalid_block_num=0;
		q_enqueue((void *)target, pri->free_segment_q);
		for(uint32_t j=0; j<BPS; j++){
			target->blocks[j]=BMM.h_block_group[j].block_set[i];
		}
		pri->num_free_seg++;
	}

	res->private_data=(void*)pri;
	return res;
}

void sbm_free (blockmanager* bm){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	mh_free(pri->max_heap);
	q_free(pri->free_segment_q);
	free(pri);
	free(bm);
}

static inline void __segment_assign_post(sbm_pri *pri, __segment *target, uint32_t type){
	switch (type){
		case BLOCK_LOAD:
		case BLOCK_RESERVE:
			break;
		case BLOCK_ACTIVE:
				mh_insert_append(pri->max_heap, (void*)target);
			break;
	}
	pri->num_free_seg--;
}

__segment* sbm_get_seg(blockmanager *bm, uint32_t type){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	__segment *res=(__segment*)q_dequeue(pri->free_segment_q);
	__segment_assign_post(pri, res, type);
	return res;
}

__segment *sbm_pick_seg(blockmanager *bm, uint32_t seg_idx, uint32_t type){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	queue *temp_free_q;
	q_init(&temp_free_q, _NOS);
	uint32_t q_size=pri->free_segment_q->size; 
	__segment *temp_free_block;
	for(uint32_t i=0; i<q_size; i++){
		temp_free_block=(__segment*)q_dequeue(pri->free_segment_q);
		if(temp_free_block->seg_idx==seg_idx){
			continue;
		}
		else{
			q_enqueue((void*)temp_free_block, temp_free_q);
			temp_free_block=NULL;
		}
	}

	q_free(pri->free_segment_q);
	pri->free_segment_q=temp_free_q;

	if(temp_free_block==NULL){
		temp_free_block=&pri->seg_set[seg_idx];
	}

	__segment_assign_post(pri, temp_free_block, type);
	return temp_free_block;
}

int32_t sbm_get_page_addr(__segment *s){
	if(s->now_assigned_bptr==BPS-1){
		if(s->blocks[BPS-1]->now_assigned_pptr==_PPB) return -1;
	}

	__block *b=s->blocks[s->now_assigned_bptr];
	if(b->now_assigned_pptr==_PPB){
		s->now_assigned_bptr++;
	}
	int blocknumber=s->now_assigned_bptr;
	b=s->blocks[blocknumber];

	uint32_t page=b->now_assigned_pptr++;
	int res=b->block_idx*_PPB+page;
	
	if(page>_PPB) abort();
	s->used_page_num++;

	if(s->used_page_num!=res%_PPS+1){
		abort();
	}
	return res;
}

int32_t sbm_pick_page_addr(__segment *s){
	if(s->now_assigned_bptr==0){
		if(s->blocks[BPS-1]->now_assigned_pptr==_PPB) return -1;
	}

	int blocknumber=s->now_assigned_bptr;
	if(s->now_assigned_bptr==BPS) s->now_assigned_bptr=0;
	__block *b=s->blocks[blocknumber];
	uint32_t page=b->now_assigned_pptr;
	int res=b->block_idx*_PPB+page;

	if(page>_PPB) abort();
	return res;
}

bool sbm_is_gc_needed(blockmanager *bm){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	return pri->free_segment_q->size==0;
}

__gsegment* sbm_get_gc_target(blockmanager* bm){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	__gsegment* res=(__gsegment*)malloc(sizeof(__gsegment));

	mh_construct(pri->max_heap);
	__segment *target=(__segment *)mh_get_max(pri->max_heap);
	if(target==NULL){
		return NULL;
	}

	memcpy(res->blocks, target->blocks, sizeof(__block*)*BPS);
	res->seg_idx=target->seg_idx;
	res->invalidate_piece_num=target->invalidate_piece_num;
	res->validate_piece_num=target->validate_piece_num;
	res->all_invalid=(res->invalidate_piece_num==res->validate_piece_num);

	for(uint32_t i=0; i<BPS; i++){
		blockmanager_full_invalid_check(res->blocks[i]);	
	}

	if(res->validate_piece_num==0){
		EPRINT("the gc target should have valid piece", true);
	}
	/*
	printf("gc_target :%u (%u:%u~%u:%u), invalidate_piece_num:%u\n", 
			res->seg_idx, 
			res->seg_idx*_PPS, res->seg_idx*_PPS/_PPB,
			(res->seg_idx+1)*_PPS-1, (res->seg_idx+1)*_PPS/_PPB,
			res->invalidate_piece_num);*/

	/*
	for(uint32_t i=0; i<_NOS; i++){
		printf("%u->%u heap:%p\n", pri->seg_set[i].seg_idx, pri->seg_set[i].invalidate_piece_num,
				pri->seg_set[i].private_data);
	}*/

	return res;
}

void sbm_trim_segment(blockmanager *bm, __gsegment *gs){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	__segment *	s=&pri->seg_set[gs->seg_idx];
	for(uint32_t i=0; i<BPS; i++){
		block_reinit(s->blocks[i]);
	}
	s->now_assigned_bptr=0;
	s->used_page_num=0;
	s->validate_piece_num=s->invalidate_piece_num=0;
	s->private_data=NULL;
	s->invalid_block_num=0;
	
	q_enqueue((void*)s, pri->free_segment_q);
	bm->li->trim_block(s->seg_idx * _PPS);
	free(gs);
}
#define EXTRACT_BID(target, ispiece) (target/(ispiece?L2PGAP:1))/_PPB
#define EXTRACT_SID(target, ispiece) (EXTRACT_BID(target,ispiece))/BPS
#define EXTRACT_INTRA_PPIDX(target, ispiece) target%(_PPB*(ispiece?L2PGAP:1))

int sbm_bit_set(struct blockmanager* bm, uint32_t piece_ppa){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	uint32_t bid=EXTRACT_BID(piece_ppa, true);
	uint32_t sid=EXTRACT_SID(piece_ppa, true);

	int res=block_bit_query(&BMM.total_block_set[bid], EXTRACT_INTRA_PPIDX(piece_ppa, true))?0:1;

	block_bit_set(&BMM.total_block_set[bid], EXTRACT_INTRA_PPIDX(piece_ppa, true));
	if (BMM.total_block_set[bid].is_full_invalid && BMM.total_block_set[bid].invalidate_piece_num != BMM.total_block_set[bid].validate_piece_num)
	{
		BMM.total_block_set[bid].is_full_invalid = false;
		pri->seg_set[sid].invalid_block_num--;
	}
	pri->seg_set[sid].validate_piece_num++;
	return res;
}

int sbm_bit_unset(struct blockmanager*bm, uint32_t piece_ppa){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	uint32_t bid=EXTRACT_BID(piece_ppa, true);
	uint32_t sid=EXTRACT_SID(piece_ppa, true);

	int res=block_bit_query(&BMM.total_block_set[bid], EXTRACT_INTRA_PPIDX(piece_ppa, true))?1:0;

	block_bit_unset(&BMM.total_block_set[bid], EXTRACT_INTRA_PPIDX(piece_ppa, true));

	if (BMM.total_block_set[bid].is_full_invalid==false && 
		BMM.total_block_set[bid].invalidate_piece_num == BMM.total_block_set[bid].validate_piece_num)
	{
		BMM.total_block_set[bid].is_full_invalid = true;
		pri->seg_set[sid].invalid_block_num++;
	}

	pri->seg_set[sid].invalidate_piece_num++;
	return res;
}

bool sbm_bit_query(struct blockmanager* bm,uint32_t piece_ppa){
	uint32_t bid=EXTRACT_BID(piece_ppa, true);
	return block_bit_query(&BMM.total_block_set[bid], EXTRACT_INTRA_PPIDX(piece_ppa, true));
}

bool sbm_is_invalid_piece(struct blockmanager* bm,uint32_t piece_ppa){
	return !sbm_bit_query(bm, piece_ppa);
}

void sbm_set_oob(struct blockmanager*,char *data, int len, uint32_t ppa){
	uint32_t bid=EXTRACT_BID(ppa, false);
	memcpy(BMM.total_block_set[bid].oob_list[EXTRACT_INTRA_PPIDX(ppa, false)].d, data, len);
}

char* sbm_get_oob(struct blockmanager*,uint32_t ppa){
	uint32_t bid=EXTRACT_BID(ppa, false);
	return BMM.total_block_set[bid].oob_list[EXTRACT_INTRA_PPIDX(ppa, false)].d;
}

void sbm_change_reserve_to_active(blockmanager *bm, __segment *s){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	mh_insert_append(pri->max_heap, (void*)s);
}

void sbm_insert_gc_target(blockmanager *bm, uint32_t seg_idx){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	mh_insert_append(pri->max_heap, (void*)&pri->seg_set[seg_idx]);
}

uint32_t sbm_total_free_page_num(blockmanager *bm, __segment *s){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	return pri->free_segment_q->size*_PPS+(s?(_PPS-s->used_page_num):0);
}

uint32_t sbm_seg_invalidate_piece_num(blockmanager *bm, uint32_t seg_idx){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	return pri->seg_set[seg_idx].invalidate_piece_num;
}

uint32_t sbm_invalidate_seg_num(blockmanager *bm){
	sbm_pri *pri=(sbm_pri*)bm->private_data;
	uint32_t res=0;
	for(uint32_t i=0; i<_NOS; i++){
		if(pri->seg_set[i].validate_piece_num!=0 &&
				pri->seg_set[i].validate_piece_num==pri->seg_set[i].invalidate_piece_num){
			res++;
		}
	}
	return res;
}

uint32_t sbm_dump(struct blockmanager *bm, FILE *fp){
	sbm_pri *p=(sbm_pri *)bm->private_data;
	blockmanager_master_dump(fp);
	for(uint32_t i=0; i<_NOS; i++){
		fwrite(&p->seg_set[i], sizeof(__segment), 1, fp);
		for(uint32_t j=0; j<BPS; j++){
			fwrite(&p->seg_set[i].blocks[j]->block_idx, 
					sizeof(p->seg_set[i].blocks[j]->block_idx), 1, fp);
		}
		if(p->seg_set[i].private_data){
			printf("[dump]%u -> assigned\n", i);
		}
		else{
			printf("[dump]%u -> not assigned\n", i);
		}
	}
	return 1;
}

uint32_t sbm_load(blockmanager *bm, FILE *fp){
	blockmanager_master_load(fp);
	sbm_pri *p=(sbm_pri *)bm->private_data;
	q_free(p->free_segment_q);
	q_init(&p->free_segment_q, _NOS);
	p->num_free_seg=0;

	__segment temp_segment;
	for(uint32_t i=0; i<_NOS; i++){
		fread(&temp_segment, sizeof(temp_segment), 1, fp);
		memcpy(&p->seg_set[i], &temp_segment, sizeof(temp_segment));

		uint32_t block_idx;
		for(uint32_t j=0; j<BPS; j++){
			fread(&block_idx, 
					sizeof(block_idx), 1, fp);
			p->seg_set[i].blocks[j]=&BMM.total_block_set[block_idx];
		}

		if(p->seg_set[i].private_data){
			mh_insert_append(p->max_heap, (void*)&p->seg_set[i]);
		}
		else{
			q_enqueue((void*)&p->seg_set[i], p->free_segment_q);
			p->num_free_seg++;
		}
		if(p->seg_set[i].private_data){
			printf("[load]%u -> assigned\n", i);
		}
		else{
			printf("[load]%u -> not assigned\n", i);
		}
	}
	return 1;
}
