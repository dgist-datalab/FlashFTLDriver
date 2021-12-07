#include "seq_block_manager.h"
#include "../../include/debug_utils.h"
#include <stdlib.h>
#include <stdio.h>

static uint64_t total_validate_piece_ppa;
static uint64_t total_invalidate_piece_ppa;

struct blockmanager seq_bm={
	.create=seq_create,
	.destroy=seq_destroy,
	.get_block=seq_get_block,
	.pick_block=seq_pick_block,
	.free_seg_num=seq_free_seg_num,
	.get_segment=seq_get_segment,
	.get_segment_target=seq_get_segment_target,
	.get_page_num=seq_get_page_num,
	.pick_page_num=seq_pick_page_num,
	.check_full=seq_check_full,
	.is_gc_needed=seq_is_gc_needed, 
	.get_gc_target=seq_get_gc_target,
	.trim_segment=seq_trim_segment,
	.free_segment=seq_free_segment,
	.populate_bit=seq_populate_bit,
	.unpopulate_bit=seq_unpopulate_bit,
	.query_bit=seq_query_bit,
	.erase_bit=seq_erase_bit,
	.is_valid_page=seq_is_valid_page,
	.is_invalid_page=seq_is_invalid_page,
	.set_oob=seq_set_oob,
	.get_oob=seq_get_oob,
	.change_reserve=seq_change_reserve,
	.reinsert_segment=seq_reinsert_segment,
	.remain_free_page=seq_remain_free_page,
	.invalidate_number_decrease=seq_invalidate_number_decrease,
	.get_invalidate_number=seq_get_invalidate_number,
	.get_invalidate_blk_number=seq_get_invalidate_blk_number,
	.load=seq_load,
	.dump=seq_dump,

	.pt_create=seq_pt_create,
	.pt_destroy=seq_pt_destroy,
	.pt_get_segment=seq_pt_get_segment,
	.pt_get_gc_target=seq_pt_get_gc_target,
	.pt_trim_segment=seq_pt_trim_segment,
	.pt_remain_page=seq_pt_remain_page,
	.pt_isgc_needed=seq_pt_isgc_needed,
	.change_pt_reserve=seq_change_pt_reserve,
	.pt_reserve_to_free=seq_pt_reserve_to_free,
};

void seq_mh_swap_hptr(void *a, void *b){
	block_set *aa=(block_set*)a;
	block_set *bb=(block_set*)b;

	void *temp=aa->hptr;
	aa->hptr=bb->hptr;
	bb->hptr=temp;
}

void seq_mh_assign_hptr(void *a, void *hn){
	block_set *aa=(block_set*)a;
	aa->hptr=hn;
}

int seq_get_cnt(void *a){
	block_set *aa=(block_set*)a;
	/*
	if(aa->total_invalid_number==UINT_MAX){
		for(uint32_t i=0; i<BPS; i++){
			res+=aa->blocks[i]->invalid_number;
		}
		aa->total_invalid_number=res;
	}
	else res=aa->total_invalid_number;*/

	return aa->total_invalid_number;
}

uint32_t seq_create (struct blockmanager* bm, lower_info *li){
	bm->li=li;
	//bb_checker_start(bm->li);/*check if the block is badblock*/
#ifdef AMF
	printf("NOC :%d _NOS:%ld\n", NOC,_NOS);
#endif

	sbm_pri *p=(sbm_pri*)malloc(sizeof(sbm_pri));
	p->seq_block=(__block*)calloc(sizeof(__block), _NOB);
	p->logical_segment=(block_set*)calloc(sizeof(block_set), _NOS);
	p->assigned_block=p->free_block=0;

	int glob_block_idx=0;
	for(int i=0; i<_NOS; i++){
		for(int j=0; j<BPS; j++){
			__block *b=&p->seq_block[glob_block_idx];
			b->block_num=i*BPS+j;
			b->bitset=(uint8_t*)calloc(_PPB*L2PGAP/8,1);
			p->logical_segment[i].blocks[j]=&p->seq_block[glob_block_idx];
			glob_block_idx++;
		}
		p->logical_segment[i].block_set_idx=i;
		p->logical_segment[i].total_invalid_number=0;
		p->logical_segment[i].total_valid_number=0;
	}

	mh_init(&p->max_heap, _NOS, seq_mh_swap_hptr, seq_mh_assign_hptr, seq_get_cnt);
	q_init(&p->free_logical_segment_q, _NOS);
	q_init(&p->invalid_block_q, _NOS);
	
	for(uint32_t i=0; i<_NOS; i++){
		q_enqueue((void*)&p->logical_segment[i], p->free_logical_segment_q);
		p->free_block++;
	}

	bm->private_data=(void*)p;
	return 1;
}

uint32_t seq_dump(struct blockmanager *bm, FILE *fp){
	sbm_pri *p=(sbm_pri *)bm->private_data;
	uint64_t temp_NOS=_NOS;
	uint64_t temp_NOB=_NOB;
	uint64_t temp_BPS=BPS;
	printf("temp_NOS:%lu, temp_NOB:%lu, temp_BPS:%lu\n", temp_NOS, temp_NOB, temp_BPS);
	fwrite(&temp_NOS, sizeof(temp_NOS), 1, fp); //write the total umber of segment
	fwrite(&temp_NOB, sizeof(temp_NOB), 1, fp); // write the total number of blocks
	fwrite(&temp_BPS, sizeof(temp_BPS), 1, fp); //write the number of blocks in a segment
	
	int glob_block_idx=0;
	for(uint32_t i=0; i<_NOS; i++){
		/*write logical_segment*/
		block_set *l_segment=&p->logical_segment[i];
		fwrite(l_segment, sizeof(block_set), 1, fp);

		for(uint32_t j=0; j<BPS; j++){
			__block *b=&p->seq_block[glob_block_idx];
			fwrite(b, sizeof(__block), 1, fp); //write a block
			fwrite(b->bitset, sizeof(uint8_t), _PPB*L2PGAP/8, fp);//write bitset
			glob_block_idx++;
		}
	}
	return 1;
}

uint32_t seq_load(struct blockmanager *bm, lower_info *li, FILE *fp){
	bm->li=li;
	uint64_t read_NOS, read_NOB, read_BPS;

	fread(&read_NOS, sizeof(read_NOS), 1, fp);
	fread(&read_NOB, sizeof(read_NOS), 1, fp);
	fread(&read_BPS, sizeof(read_NOS), 1, fp);
	printf("read_NOS:%lu, read_NOB:%lu, read_BPS:%lu\n", read_NOS, read_NOB, read_BPS);

	if(read_NOS!=_NOS || read_NOB!=_NOB || read_BPS!=BPS){
		EPRINT("different device setting", true);
	}
	
	if(bm->private_data){
		EPRINT("already block manager data exists", true);
	}

	sbm_pri *p=(sbm_pri*)malloc(sizeof(sbm_pri));
	p->seq_block=(__block*)calloc(sizeof(__block), _NOB);
	p->logical_segment=(block_set*)calloc(sizeof(block_set), _NOS);
	p->assigned_block=p->free_block=0;

	mh_init(&p->max_heap, _NOS, seq_mh_swap_hptr, seq_mh_assign_hptr, seq_get_cnt);
	q_init(&p->free_logical_segment_q, _NOS);
	q_init(&p->invalid_block_q, _NOS);

	int global_block_idx=0;
	for(uint32_t i=0; i<_NOS; i++){
		block_set *l_segment=&p->logical_segment[i];
		fread(l_segment, sizeof(block_set), 1, fp);
		l_segment->hptr=NULL;

		for(uint32_t j=0; j<BPS; j++){
			__block *b=&p->seq_block[global_block_idx];
			l_segment->blocks[j]=b;
			fread(b, sizeof(__block), 1, fp);
			b->bitset=(uint8_t*)calloc(_PPB*L2PGAP/8,1);
			fread(b->bitset, sizeof(uint8_t), _PPB * L2PGAP/8, fp);
			b->hptr=NULL;
			global_block_idx++;
		}
	}

	for(uint32_t i=0; i<_NOS; i++){
		block_set *l_segment=&p->logical_segment[i];

		if(l_segment->total_invalid_number && l_segment->total_invalid_number == l_segment->total_valid_number){
			q_enqueue((void*)l_segment, p->invalid_block_q);
			p->assigned_block++;
		}
		else if(l_segment->isused==false){
			q_enqueue((void*)l_segment, p->free_logical_segment_q);	
			p->free_block++;
		}
		else{
			mh_insert_append(p->max_heap,(void*)l_segment);
			p->assigned_block++;
		}
	}

	bm->private_data=(void*)p;
	return 1;
}

uint32_t seq_destroy (struct blockmanager* bm){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	for(uint32_t i=0; i<BPS*_NOS; i++){
		__block *b=&p->seq_block[i];
		free(b->bitset);
	}
	free(p->seq_block);
	free(p->logical_segment);
	mh_free(p->max_heap);
	q_free(p->free_logical_segment_q);
	free(p);
	bm->private_data=NULL;
	return 1;
}

__block* seq_get_block (struct blockmanager* bm, __segment* s){
	if(s->now+1>s->max) abort();
	return s->blocks[s->now++];
}

__segment *get_segment_body(block_set *free_block_set, sbm_pri *p, uint32_t type){
	__segment* res=(__segment*)malloc(sizeof(__segment));
	if(!free_block_set){
		EPRINT("dev full??", false);
		return NULL;
	}

	if(type!=BLOCK_LOAD && (free_block_set->total_invalid_number || free_block_set->total_valid_number)){
		EPRINT("how can it be!\n", true);
	}

	if(!free_block_set){
		printf("new block is null!\n");
		abort();
	}


	if(type==BLOCK_RESERVE){

	}
	else{
		free_block_set->isused=true;
		mh_insert_append(p->max_heap, (void*)free_block_set);
	}

	memcpy(res->blocks, free_block_set->blocks, sizeof(__block*)*BPS);

	res->now=0;
	res->max=BPS;
	res->invalid_blocks=0;
	res->used_page_num=0;
	res->seg_idx=res->blocks[0]->block_num/BPS;

	if(type==BLOCK_LOAD){
		for(uint32_t i=0; i<BPS; i++){
			__block *b=free_block_set->blocks[i];
			if(b->now==_PPB){
				res->now++;
			}
			res->used_page_num+=b->now;
		}
	}
	return res;
}

__segment* seq_get_segment (struct blockmanager* bm, uint32_t type){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	
	block_set *free_block_set=(block_set*)q_dequeue(p->free_logical_segment_q);
	__segment* res=get_segment_body(free_block_set, p, type);

	p->assigned_block++;
	p->free_block--;


	if(p->assigned_block+p->free_block!=_NOS){
		printf("missing segment error\n");
		abort();
	}
	return res;
}

__segment* seq_get_segment_target (struct blockmanager* bm, uint32_t seg_idx, uint32_t type){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	queue *temp_free_block;
	q_init(&temp_free_block, _NOS);
	
	block_set *free_block_set=NULL;
	uint32_t q_size=p->free_logical_segment_q->size; 
	for(uint32_t i=0; i<q_size; i++){
		free_block_set=(block_set*)q_dequeue(p->free_logical_segment_q);
		if(free_block_set->block_set_idx==seg_idx){
			break;
		}
		else{
			q_enqueue((void*)free_block_set, temp_free_block);
		}
	}

	__segment* res=get_segment_body(free_block_set, p, type);

	q_size=p->free_logical_segment_q->size;
	for(uint32_t i=0; i<q_size; i++){
		free_block_set=(block_set*)q_dequeue(p->free_logical_segment_q);
		q_enqueue((void*)free_block_set, temp_free_block);
	}

	
	q_free(p->free_logical_segment_q);
	p->free_logical_segment_q=temp_free_block;

	p->assigned_block++;
	p->free_block--;


	if(p->assigned_block+p->free_block!=_NOS){
		printf("missing segment error\n");
		abort();
	}
	return res;
}

__segment* seq_change_reserve(struct blockmanager* bm,__segment *reserve){

	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t segment_start_block_number=reserve->blocks[0]->block_num;
	uint32_t segment_idx=segment_start_block_number/BPS;
	block_set *bs=&p->logical_segment[segment_idx];

	bs->isused=true;
	mh_insert_append(p->max_heap, (void*)bs);

	return seq_get_segment(bm, BLOCK_RESERVE);
}


void seq_reinsert_segment(struct blockmanager *bm, uint32_t seg_idx){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	block_set *bs=&p->logical_segment[seg_idx];

	bs->isused=true;
	mh_insert_append(p->max_heap, (void*)bs);
}


bool seq_is_gc_needed (struct blockmanager* bm){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	if(p->free_logical_segment_q->size==0) return true;
	return false;
}

uint32_t seq_free_seg_num(struct blockmanager* bm){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	return p->free_logical_segment_q->size;
}
void temp_print(void *a){
	if(a){
		static int cnt=0;
		block_set* target=(block_set*)a;
		printf("[heap_print:%d] block_num:%u invalidate_number:%u\n", cnt++,
				target->blocks[0]->block_num/BPS,
				target->total_invalid_number);
	}
}

__gsegment* seq_get_gc_target (struct blockmanager* bm){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	__gsegment* res=(__gsegment*)malloc(sizeof(__gsegment));
	res->invalidate_number=0;

	block_set *target=NULL;
	if(p->invalid_block_q->size){
		target=(block_set*)q_dequeue(p->invalid_block_q);
	}
	else{
		mh_construct(p->max_heap);
		target=(block_set*)mh_get_max(p->max_heap);
	}

	if(!target) return NULL;

	memcpy(res->blocks, target->blocks, sizeof(__block*)*BPS);
	res->now=res->max=0;
	res->seg_idx=res->blocks[0]->block_num/BPS;

	res->invalidate_number=target->total_invalid_number;
	res->validate_number=target->total_valid_number;

	if(target->total_invalid_number==target->total_valid_number){
		res->all_invalid=true;
	}
	else{
		res->all_invalid=false;
	}

	if(res->invalidate_number==0){
		/*
		printf("_NOS*_PPS*L2PGAP:%lu validate:%lu ivnalidate:%lu\n", 
			(uint64_t)_NOS*_PPS*L2PGAP, total_validate_piece_ppa, total_invalidate_piece_ppa);

		mh_print(p->max_heap, temp_print);
		for(uint32_t i=0; i<_NOS; i++){
			printf("[seg:%u] validate_num:%u invalidate_num:%u\n", i,
					p->logical_segment[i].total_valid_number,
					p->logical_segment[i].total_invalid_number);	
		}
		EPRINT("dev full", false);*/
		mh_construct(p->max_heap);
	}

	return res;
}

uint32_t seq_get_invalidate_blk_number(struct blockmanager *bm){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	mh_construct(p->max_heap);
	while(1){
		block_set* target=(block_set*)mh_get_max(p->max_heap);

		if(target->total_invalid_number==target->total_valid_number){
			q_enqueue((void*)target, p->invalid_block_q);
		}
		else{
			uint32_t seg_idx=target->blocks[0]->block_num/BPS;
			seq_reinsert_segment(bm, seg_idx);
			break;
		}
	}

	return p->invalid_block_q->size;
}

void seq_trim_segment (struct blockmanager* bm, __gsegment* gs, struct lower_info* li){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t segment_startblock_number=gs->blocks[0]->block_num;

	for(int i=0; i<BPS; i++){
		__block *b=gs->blocks[i];
		b->invalidate_number=0;
		b->validate_number=0;
		b->now=0;
		memset(b->bitset,0,_PPB*L2PGAP/8);
		memset(b->oob_list,-1,sizeof(b->oob_list));
	}

	uint32_t segment_idx=segment_startblock_number/BPS;
	block_set *bs=&p->logical_segment[segment_idx];
	bs->total_invalid_number=0;
	bs->total_valid_number=0;
	bs->isused=false;
	/*
	if(bs==&p->logical_segment[1228928/16384]){
		bs->blocks[(1228928%16384)%256]
	}*/
	q_enqueue((void*)bs, p->free_logical_segment_q);
	
	p->assigned_block--;
	p->free_block++;

	if(p->assigned_block+p->free_block!=_NOS){
		printf("missing segment error\n");
		abort();
	}

	li->trim_block(segment_startblock_number*_PPB);
	free(gs);
}

int seq_populate_bit (struct blockmanager* bm, uint32_t ppa){
	int res=1;
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bn=ppa/(_PPB * L2PGAP);
	uint32_t pn=ppa%(_PPB * L2PGAP);
	uint32_t bt=pn/8;
	uint32_t of=pn%8;

	__block *b=&p->seq_block[bn];
	b->validate_number++;
	uint32_t segment_idx=b->block_num/BPS;
	block_set *seg=&p->logical_segment[segment_idx];
	seg->total_valid_number++;
	total_validate_piece_ppa++;

	if(p->seq_block[bn].bitset[bt]&(1<<of)){
		res=0;
	//	abort();
	}
	p->seq_block[bn].bitset[bt]|=(1<<of);
	return res;
}

int seq_unpopulate_bit (struct blockmanager* bm, uint32_t ppa){
	int res=1;
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bn=ppa/(_PPB * L2PGAP);
	uint32_t pn=ppa%(_PPB * L2PGAP);
	uint32_t bt=pn/8;
	uint32_t of=pn%8;
	__block *b=&p->seq_block[bn];

	if(!(p->seq_block[bn].bitset[bt]&(1<<of))){
		res=0;
		//abort();
	}

	b->bitset[bt]&=~(1<<of);
	b->invalidate_number++;

	uint32_t segment_idx=b->block_num/BPS;
	block_set *seg=&p->logical_segment[segment_idx];
	seg->total_invalid_number++;
	total_invalidate_piece_ppa++;

	if(b->invalidate_number>_PPB * L2PGAP){
	//	printf("????\n");
		//abort();
	}
	return res;
}


bool seq_query_bit(struct blockmanager *bm, uint32_t ppa){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bn=ppa/(_PPB * L2PGAP);
	uint32_t pn=ppa%(_PPB * L2PGAP);
	uint32_t bt=pn/8;
	uint32_t of=pn%8;

	return p->seq_block[bn].bitset[bt]&(1<<of);
}


void seq_invalidate_number_decrease(struct blockmanager *bm, uint32_t ppa){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bn=ppa/(_PPB * L2PGAP);
	//uint32_t pn=ppa%(_PPB * L2PGAP);
	//uint32_t bt=pn/8;
	//uint32_t of=pn%8;
	__block *b=&p->seq_block[bn];
	b->invalidate_number--;


	uint32_t segment_idx=b->block_num/BPS;
	block_set *seg=&p->logical_segment[segment_idx];
	seg->total_invalid_number--;

}

int seq_erase_bit (struct blockmanager* bm, uint32_t ppa){
	int res=1;
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bn=ppa/(_PPB * L2PGAP);
	uint32_t pn=ppa%(_PPB * L2PGAP );
	uint32_t bt=pn/8;
	uint32_t of=pn%8;
	__block *b=&p->seq_block[bn];

	if(!(p->seq_block[bn].bitset[bt]&(1<<of))){
		res=0;
	}
	b->bitset[bt]&=~(1<<of);
	if(0<=ppa && ppa< _PPS*MAPPART_SEGS){
		if(b->invalidate_number>_PPB){
			abort();
		}
	}
	return res;
}

bool seq_is_valid_page (struct blockmanager* bm, uint32_t ppa){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bn=ppa/(_PPB*L2PGAP);
	uint32_t pn=ppa%(_PPB * L2PGAP);
	uint32_t bt=pn/8;
	uint32_t of=pn%8;

	return p->seq_block[bn].bitset[bt]&(1<<of);
}

bool seq_is_invalid_page (struct blockmanager* bm, uint32_t ppa){
	return !seq_is_valid_page(bm,ppa);
}

void seq_set_oob(struct blockmanager* bm, char *data,int len, uint32_t ppa){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	__block *b=&p->seq_block[ppa/_PPB];
	memcpy(b->oob_list[ppa%_PPB].d,data,len);
}



char *seq_get_oob(struct blockmanager*bm,  uint32_t ppa){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	uint32_t bidx=ppa/_PPB;
	__block *b=&p->seq_block[bidx];
	return b->oob_list[ppa%_PPB].d;
}

void seq_release_segment(struct blockmanager* bm, __segment *s){
	free(s);
}

int seq_get_page_num(struct blockmanager* bm,__segment *s){
	if(s->now==BPS-1){
		if(s->blocks[BPS-1]->now==_PPB) return -1;
	}

#ifdef LSM_DEBUG
	uint32_t total_num=0;	
	for(uint32_t i=0; i<BPS; i++){
		total_num+=s->blocks[i]->now;
	}
	if(total_num!=s->used_page_num){
		for(uint32_t i=0; i<BPS; i++){
			//total_num+=s->blocks[i]->now;
			printf("ttt: %u:%u\n", i, s->blocks[i]->now);
		}
		abort();
	}
#endif


	__block *b=s->blocks[s->now];
	if(b->now==_PPB){
		s->now++;
	}
	int blocknumber=s->now;
	b=s->blocks[blocknumber];

	uint32_t page=b->now++;
	int res=b->block_num*_PPB+page;
	
	if(page>_PPB) abort();
	s->used_page_num++;

	if(s->used_page_num!=res%_PPS+1){
		abort();
	}
	return res;
}

int seq_pick_page_num(struct blockmanager* bm,__segment *s){
	if(s->now==0){
		if(s->blocks[BPS-1]->now==_PPB) return -1;
	}

	int blocknumber=s->now;
	if(s->now==BPS) s->now=0;
	__block *b=s->blocks[blocknumber];
	uint32_t page=b->now;
	int res=b->block_num*_PPB+page;

	if(page>_PPB) abort();
	return res;
}

bool seq_check_full(struct blockmanager *bm,__segment *active, uint8_t type){
	bool res=false;
//	__block *b=active->blocks[active->now];
	switch(type){
		case MASTER_SEGMENT:
			break;
		case MASTER_PAGE:
		case MASTER_BLOCK:
			if(active->blocks[BPS-1]->now==_PPB){
				res=true;
			}
			break;
			/*
			if(active->now >= active->max){
				res=true;
				abort();
			}
			break;*/
	}
	return res;
}

__block *seq_pick_block(struct blockmanager *bm, uint32_t page_num){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	return &p->seq_block[page_num/_PPB];
}

void seq_free_segment(struct blockmanager *, __segment *seg){
	free(seg);
}

uint32_t seq_remain_free_page(struct blockmanager *bm, __segment *active){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	return p->free_block*_PPS+(active?(_PPS-active->used_page_num):0);
}

uint32_t seq_get_invalidate_number(struct blockmanager *bm, uint32_t seg_idx){
	sbm_pri *p=(sbm_pri*)bm->private_data;
	return p->logical_segment[seg_idx].total_invalid_number;
}

