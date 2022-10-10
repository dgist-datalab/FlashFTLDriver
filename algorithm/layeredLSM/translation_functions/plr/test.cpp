#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <set>

#define RANGE 1024*1024*1024/4
int main(){
	srand(time(NULL));
	std::set<uint32_t>* lba_set=new std::set<uint32_t>();

	while(lba_set->size()< RANGE/14/14){
		uint32_t lba=rand();
		lba%=RANGE;
		lba_set->insert(lba);
	}

	std::set<uint32_t>::iterator it;
	uint32_t i=0;
	uint32_t delta=0;
	uint32_t prev=0;
	for(it=lba_set->begin(); it!=lba_set->end(); it++, i++){
		if(i==0){
			prev=*it;
			continue;
		}
		else{
			if(delta < *it-prev){
				delta=*it-prev;
			}
			prev=*it;
		}
	}
	
	printf("%u\n", delta);

	while(lba_set->size()< RANGE/14){
		uint32_t lba=rand();
		lba%=RANGE;
		lba_set->insert(lba);
	}

	i=0;
	delta=0;
	prev=0;

	for(it=lba_set->begin(); it!=lba_set->end(); it++, i++){
		if(i==0){
			prev=*it;
			continue;
		}
		else{
			if(delta < *it-prev){
				delta=*it-prev;
			}
			prev=*it;
		}
	}
	
	printf("%u\n", delta);
}
