#include "./bf.h"
#include <math.h>

#define ABS(a) (a<0? -a:a)
#define EPSILON 0.00000000001f

static inline double func(double x, uint32_t member, float target_fpr){
	return pow(x, member)+member*(target_fpr-1)*x-(target_fpr*member-member+1);
}

static inline double deriv_func(double x, uint32_t member, float target_fpr){
	return pow(x, member-1)+member*(target_fpr-1);
}

static inline double newton(double init_value, uint32_t member, float target_fpr){
	double h, res;
	uint32_t cnt=0;
	do{
		cnt++;
		h=func(res, member, target_fpr) / deriv_func(res, member, target_fpr);
//		printf("h: %lf res: %lf\n", h, res);
		if(ABS(h) >= EPSILON){
			res-=h;
		}
		else break;

		if(cnt>100)break;
	}while(1);

	return 1-res;
}

double get_target_each_fpr(uint32_t member_num, float block_fpr){
	return newton(0.5, member_num, block_fpr);
}

double get_number_of_bits(double target_fpr){
	double t=1/target_fpr;
	//uint64_t tt=ceil(log2(t));
	//printf("tt:%u\n",tt);
	return ceil(log2(t));//tt;//pow(2,log2(t)) < t ? tt+1: tt;
}

bloom_filter_meta *bf_parameter_setting(uint32_t contents_num, float fpr){
	bloom_filter_meta * res=(bloom_filter_meta*)malloc(sizeof(bloom_filter_meta));
	res->entry_fpr=get_target_each_fpr(contents_num, fpr);
	res->real_bits_per_entry=get_number_of_bits(res->entry_fpr);
	res->bits=ceil(res->real_bits_per_entry);
	//res->contents_num=contents_num;
	return res;
}

void bf_parameter_free(bloom_filter_meta *meta){
	free(meta);
}

static inline uint32_t hashfunction(uint32_t key){
	key ^= key >> 15;
	key *= UINT32_C(0x2c1b3c6d);
	key ^= key >> 12;
	key *= UINT32_C(0x297a2d39);
	key ^= key >> 15;
	
	key = ~key + (key << 15); // key = (key << 15) - key - 1;
	key = key ^ (key >> 12);
	key = key + (key << 2);
	key = key ^ (key >> 4);
	key = key * 2057; // key = (key + (key << 3)) + (key << 11);
	key = key ^ (key >> 16);
	return key;
}

void bf_set(bloom_filter_meta *bfm, bloom_filter *bf, uint32_t lba){
	bf->symbolized=(hashfunction(lba) & ((1<<(bfm->bits))-1));
}

bool bf_check(bloom_filter_meta *bfm, bloom_filter *bf, uint32_t lba){
	return (bf->symbolized == (hashfunction(lba) & ((1<<(bfm->bits))-1)));
}

/*
uint32_t bf_bit_per_entry(float fpr, uint32_t bit_for_psa){

}
*/
