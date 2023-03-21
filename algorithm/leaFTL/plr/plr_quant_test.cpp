#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <set>
#include "./plr.h"

std::set<uint32_t> random_set;

#define MAX_RANGE (64*1024*1024/4)
#define TARGET_ITEM_NUM (MAX_RANGE/10)

int main(){
    while(random_set.size() < TARGET_ITEM_NUM){
        random_set.insert(rand()%MAX_RANGE);
    }

    for(uint32_t i=3; i<15; i++){
        PLR *plr=new PLR(i,5);
        std::set<uint32_t>::iterator iter;
        uint32_t ppa=0;
        for(iter=random_set.begin(); iter!=random_set.end(); iter++){
            plr->insert(*iter, ppa++/4);
        }
        plr->insert_end();
        uint32_t error_rate=0;
        ppa=0;
        for(iter=random_set.begin(); iter!=random_set.end(); iter++){
            if(plr->get(*iter)!=ppa++/4){
                error_rate++;
            }
        }
        printf("%u,%f,%f\n", i, (double)plr->memory_usage(33)/random_set.size(), (double)error_rate/random_set.size());
        delete plr;
    }

    for(uint32_t i=3; i<16; i++){
        PLR *plr=new PLR(11,5);
        plr->set_X_bit(i);
        plr->set_Y_bit(i+2);
        std::set<uint32_t>::iterator iter;
        uint32_t ppa=0;
        for(iter=random_set.begin(); iter!=random_set.end(); iter++){
            plr->insert(*iter, ppa++/4);
        }
        plr->insert_end();
        printf("%u,%lu\n", i, plr->get_chunk_cnt());
        delete plr;
    }

    for(uint32_t i=10; i<1000; i+=10){
        PLR *plr=new PLR(11,5);
        plr->set_delta_size(i);
        std::set<uint32_t>::iterator iter;
        uint32_t ppa=0;
        for(iter=random_set.begin(); iter!=random_set.end(); iter++){
            plr->insert(*iter, ppa++/4);
        }
        plr->insert_end();
        printf("%u,%lu,%lu,%f\n", i, plr->get_chunk_cnt(), plr->get_line_cnt(), (double)plr->memory_usage(33)/random_set.size());
        delete plr;
    }
}