#include "plr.h"
//#include "zipfian.h"
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <set>

//#define LIMIT_LBA_NUM 1000000
//#define RANGE 64*1024*1024/4
#define RANGE 10000
#define LIMIT_ROUND 1000

int main(){
	
	srand(time(NULL));
	std::set<uint32_t>* lba_set=new std::set<uint32_t>();

	PLR temp_plr(7,5);
	temp_plr.insert(1,0);
	temp_plr.insert_end();
	//printf("line %u\n", temp_plr.get_line_cnt());

	double prev_merged_entry=0;
	for(uint32_t i=1; i<=LIMIT_ROUND; i++){
		uint64_t LIMIT_LBA_NUM=RANGE/LIMIT_ROUND * i;
	//	printf("%u\n", RANGE);
		while(lba_set->size()<LIMIT_LBA_NUM){
			uint32_t lba=rand();
			lba%=RANGE;
			lba_set->insert(lba);
		}
	//	printf("done1\n");

		PLR *plr=new PLR(7, 5);
		
		std::set<uint32_t>::iterator it;
		uint32_t ppa=0;
	
		for(it=lba_set->begin(); it!=lba_set->end(); it++){
		//	printf("%d\n" ,*it);
			plr->insert(*it, ppa++/4);
		}
		
		plr->insert_end();
		double now_merged_entry=LIMIT_LBA_NUM/plr->get_line_cnt();
		if(prev_merged_entry < now_merged_entry){
			prev_merged_entry=now_merged_entry;
		}
		printf("%u %.2f %lu %.2f\n",i, (double)LIMIT_LBA_NUM/plr->get_line_cnt(), plr->get_line_cnt(), (double)plr->get_normal_memory_usage(64)/LIMIT_LBA_NUM);

		//printf("%u %.2f %.2f\n",i, (double)plr->memory_usage(48)/LIMIT_LBA_NUM, (double)plr->get_normal_memory_usage(48)/LIMIT_LBA_NUM);
		//printf("entry per line: %.2f (%u)\n", (double)LIMIT_LBA_NUM/plr->get_line_cnt(), plr->get_line_cnt());
		delete plr;
	}
	delete lba_set;

//	printf("memroy usage: %lf\n", (double)plr.memory_usage(32)/LIMIT_LBA_NUM);
}
