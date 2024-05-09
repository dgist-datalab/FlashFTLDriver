#include "plr.h"
//#include "zipfian.h"
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include <map>
#include <vector>
#include <algorithm>

#define MAX_SIZE 1024*1024
#define MAX_DISTANCE 7
#define MAX_RANGE (MAX_SIZE*MAX_DISTANCE)

int main(){
	srand(time(NULL));
	std::map<uint32_t, uint32_t> lba_set;
    std::vector<uint32_t> lba_vector;



    for(uint32_t i=1; i<100; i++){
        lba_set.clear();
        lba_vector.clear();

        while(lba_set.size()<MAX_SIZE){
            uint32_t target_lba=rand()%MAX_RANGE;
            if(lba_set.count(target_lba)==0){
                lba_set.insert(std::make_pair(target_lba, 0));
                lba_vector.push_back(target_lba);
            }
        }

        std::sort(lba_vector.begin(), lba_vector.end());


        uint32_t target_error=5*i;
        PLR *plr=new PLR(7, target_error);

        std::map<uint32_t, uint32_t>::iterator it;
        uint32_t ppa=0;
        for(it=lba_set.begin(); it!=lba_set.end(); it++){
            it->second=ppa++/4;
            plr->insert(it->first, it->second);
        }
        plr->insert_end();

        uint32_t error_num1=0, error_num2=0;
        uint32_t not_found_case1=0, not_found_case2=0;
        //sequential
        for(auto a:lba_vector){
            uint32_t target=a;
            if(plr->get(target)!=lba_set.find(target)->second){
                error_num1++;
                if(abs((int)plr->get(target)-(int)lba_set.find(target)->second)>1){
                    not_found_case1++;
                }
            }

            uint32_t target_idx=rand()%(MAX_SIZE);
            target=lba_vector[target_idx];
            if(plr->get(target)!=lba_set.find(target)->second){
                error_num2++;
                if(abs((int)plr->get(target)-(int)lba_set.find(target)->second)>1){
                    not_found_case2++;
                }
            }
        }

        printf("%u %lf(%lf) %lf(%lf) %lf %u\n", target_error, (double)error_num1/lba_vector.size(), (double)not_found_case1/error_num1, (double)error_num2/lba_vector.size(),(double)not_found_case2/error_num2, (double)plr->memory_usage(32)/lba_vector.size(), plr->get_line_cnt());
        delete plr;
    }
}