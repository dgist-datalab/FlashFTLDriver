#include "lsmtree_param_module.h"
#include "../../../include/settings.h"
#include <cmath>
static inline int checking(double level, double size_factor, double blocknumber){
	uint32_t head_num=0;
	double level_head_num=1;
	for(uint32_t i=0; i<=level; i++){
		head_num+=level_head_num;
		level_head_num*=size_factor;
	}
	if(head_num<blocknumber){
		return -1;
	}
	else if(head_num > blocknumber){
		return 1;
	}
	else return 0;
}

double get_double_size_factor(double level,  double blocknumber){
	double target=(std::pow(blocknumber, (double)1.0/level));
	int result;
	int retry_cnt=0;
retry:
	switch((result=checking(level, target, blocknumber))){
		case 1:
		case 0:
			return target;
		case -1:
			target++;
			retry_cnt++;
			if(retry_cnt>2) return UINT32_MAX;
			goto retry;
	}
	return UINT32_MAX;
}

double get_double_level(double sizefactor, double blocknumber){
	double target=(std::log(blocknumber)/std::log((double)sizefactor));
	int result;
	int retry_cnt=0;
retry:
	switch((result=checking(target, sizefactor, blocknumber))){
		case 1:
		case 0:
			return target;
		case -1:
			target++;
			retry_cnt++;
			if(retry_cnt>2) return UINT32_MAX;
			goto retry;
	}
	return UINT32_MAX;
}









































































































