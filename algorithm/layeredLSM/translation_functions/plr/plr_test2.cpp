#include "plr.h"
#include <stdio.h>
#include <stdlib.h>
#include <set>

#define LIMIT_LBA_NUM 1000000
#define RANGE 64*1024*1024/4

int main(){
	srand(time(NULL));
	std::set<uint32_t> lba_set;
	printf("%u\n", RANGE);
	while(lba_set.size()<LIMIT_LBA_NUM){
		uint32_t lba=rand();
		lba%=RANGE;
		lba_set.insert(lba);
		if(lba_set.size()%100==0){
			printf("lba size %u\n", lba_set.size());
		}
	}
	printf("done1\n");

	PLR plr(7, 5);
	
	std::set<uint32_t>::iterator it;
	uint32_t ppa=0;

	for(it=lba_set.begin(); it!=lba_set.end(); it++){
		plr.insert(*it, ppa++/4);
	}

	plr.insert_end();
	printf("done2\n");

	ppa=0;
	uint32_t error_num=0;
	for(it=lba_set.begin(); it!=lba_set.end(); it++){
		uint32_t result=plr.get(*it);
		if(result!=ppa++){
			error_num++;
		}
	}
	
	printf("memroy usage: %lf\n", (double)plr.memory_usage(32)/LIMIT_LBA_NUM);
	printf("error rate :%lf\n", (double)error_num/LIMIT_LBA_NUM);
}
