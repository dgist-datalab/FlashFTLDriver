#include "plr_memory_calculator.h"
#include "../translation_functions/plr/plr.h"
#include <stdlib.h>
#include <set>
static uint32_t plr_target_range[]={5, 35, 80, 140, 190};
using namespace std;
uint32_t *random_seq;
void plr_memory_calc_init(uint32_t DEV_size){
	random_seq=(uint32_t*)malloc(sizeof(uint32_t)*DEV_size);
	for(uint32_t i=0; i<DEV_size; i++){
		random_seq[i]=i;	
	}

	for(volatile uint32_t i=0; i<DEV_size*10; i++){
		uint32_t a=random_seq[rand()%DEV_size];
		uint32_t b=random_seq[rand()%DEV_size];
		uint32_t temp=random_seq[a];
		random_seq[a]=random_seq[b];
		random_seq[b]=temp;
		if(i%DEV_size==0){
			printf("\r%u round done!", i/DEV_size);
			fflush(stdout);
		}
	}
	printf("\n");
}

int compare(const void* _a, const void *_b){
	uint32_t a=*(uint32_t*)_a;
	uint32_t b=*(uint32_t*)_b;
	if(a<b) return -1;
	else if(a>b) return 1;
	return 0;
}

uint64_t plr_memory_calc(uint32_t entry_num, 
		uint32_t error, uint32_t DEV_size, bool wiskey){

	
	uint32_t *target=(uint32_t*)malloc(sizeof(uint32_t)*entry_num);
	memcpy(target, random_seq, sizeof(uint32_t)*entry_num);

	qsort(target, entry_num, sizeof(uint32_t), compare);

	PLR *plr=new PLR(8,error/2);
	for(uint32_t i=0; i<entry_num; i++){
		plr->insert(target[i], wiskey?i:i/4);
	}
	plr->insert_end();
	uint64_t res=plr->memory_usage(48);
	delete plr;
	free(target);
	return res+(wiskey?entry_num*48:0);
}
double plr_memory_calc_avg(uint32_t entry_num, uint32_t target_bit,
		uint32_t error, uint32_t DEV_size, bool wiskey, 
		double *line_per_chunk, double *normal_plr_dp,
		uint64_t *line_cnt, uint64_t* chunk_cnt){

	uint32_t *target=(uint32_t*)malloc(sizeof(uint32_t)*entry_num);
	memcpy(target, random_seq, sizeof(uint32_t)*entry_num);

	qsort(target, entry_num, sizeof(uint32_t), compare);

	PLR *plr=new PLR(8,plr_target_range[error/10-1]);
	for(uint32_t i=0; i<entry_num; i++){
		plr->insert(target[i], wiskey?i:i/4);
	}
	plr->insert_end();
	double res=plr->memory_usage(target_bit);
	double normal_memory=plr->get_normal_memory_usage(target_bit);
	*line_per_chunk=plr->get_line_per_chunk();
	*line_cnt=plr->get_line_cnt();
	*chunk_cnt=plr->get_chunk_cnt();

	delete plr;
	free(target);
	*normal_plr_dp=normal_memory/entry_num;
	return (res+(wiskey?entry_num*target_bit:0))/entry_num;
}
