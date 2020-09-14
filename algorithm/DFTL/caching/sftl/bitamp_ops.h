#include "../../../../include/data_struct/bitmap.h"

#define for_each_bitmap_backward(map, offset, target)\
	for(target=bitmap_is_set(map, offset--); offset >= 0 && offset!=UINT32_MAX; target=bitmap_is_set(map,offset--))

static inline uint32_t get_distance(bitmap *map ,uint32_t lba){
	uint32_t cnt=0;
	bool isset;
	int32_t offset=TRANSOFFSET(lba);
	for_each_bitmap_backward(map, offset, isset){
		if(!isset) cnt++;
		else break;
	}
	return cnt;
}

