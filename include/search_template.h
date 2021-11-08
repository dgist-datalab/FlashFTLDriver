#include <stdint.h>
#define bs_search(body, s, m, target, cmp, target_idx)\
	do{\
		uint32_t __start=s;\
		uint32_t __end=m;\
		uint32_t __mid;\
		int res;\
		while(__start<=__end){\
			__mid=(__start+__end)/2;\
			res=cmp((body)[__mid], target);\
			if(res==0){\
				(target_idx)=__mid;\
				break;\
			}\
			else if(res<0){\
				__start=__mid+1;\
			}\
			else{\
				__end=__mid-1;\
			}\
		}\
		if(__start>__end) (target_idx)=UINT32_MAX;\
	}while(0)
