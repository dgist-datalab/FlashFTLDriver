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
		if(__start>__end){\
			(target_idx)=UINT32_MAX;\
		}\
	}while(0)

#define bs_lower_bound(body, s, m, target, cmp, target_idx)\
	do{\
		int32_t __start=s;\
		int32_t __end=m+1;\
		int32_t __mid;\
		int __res;\
		while(__start < __end){\
			__mid=__start+(__end-__start)/2;\
			__res=cmp((body)[__mid], target);\
			if(__res>=0){\
				__end=__mid;\
			}\
			else __start=__mid+1;\
		}\
		(target_idx)=__start;\
	}while(0)

/*
static inline void  bs_lower_bound(uint32_t *body, 
		uint32_t s, uint32_t m, uint32_t target,
		int32_t (*cmp)(uint32_t, uint32_t), uint32_t *target_idx){\
	do{\
		int32_t __start=s;\
		int32_t __end=m+1;\
		int32_t __mid;\
		int __res;\
		while(__start < __end){\
			__mid=__start+(__end-__start)/2;\
			__res=cmp((body)[__mid], target);\
			if(__res>=0){\
				__end=__mid;\
			}\
			else __start=__mid+1;\
		}\
		(*target_idx)=__start;\
		if((*target_idx) > m) (*target_idx)=-1;\
	}while(0);
}
*/
