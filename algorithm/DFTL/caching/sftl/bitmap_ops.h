#include "../../../../include/data_struct/bitmap.h"

#define for_each_bitmap_backward(map, offset, target)\
	for(target=bitmap_is_set(map, offset--); offset >= 0 && offset!=UINT32_MAX; target=bitmap_is_set(map,offset--))

#define for_each_bitmap_forward(map, offset, target, max)\
	for(offset=0, target=bitmap_is_set(map, offset++); offset<max ; target=bitmap_is_set(map,offset++))

static inline uint32_t get_distance(bitmap *map ,uint32_t lba){
	uint32_t cnt=0;
	bool isset;
	int32_t offset=GETOFFSET(lba);
	for_each_bitmap_backward(map, offset, isset){
		if(!isset) cnt++;
		else break;
	}
	return cnt;
}

static inline uint32_t get_head_offset(bitmap *map, uint32_t lba){
	uint32_t cnt=0;
	bool isset;
	int32_t max=GETOFFSET(lba);
	uint32_t offset=0;
	for_each_bitmap_forward(map, offset, isset, max){
		if(isset) cnt++;
	}
	return cnt;
}

