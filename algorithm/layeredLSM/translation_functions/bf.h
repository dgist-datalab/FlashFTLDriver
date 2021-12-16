#ifndef BF_HEADER
#define BF_HEADER

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct bloom_filter{
	uint32_t symbolized;
}bloom_filter;

typedef struct bloom_filter_meta{
	double real_bits_per_entry;
	float entry_fpr;
	uint32_t bits;
}bloom_filter_meta;

/*
 * Function: bf_parameter_setting
 * ------------------------------
 *		return target bloom_filter_meta 
 *
 * contents_num: the max number of bloom_filter set
 * target_fpr: the fpr of bloom_filter set
 * */
bloom_filter_meta *bf_parameter_setting(uint32_t contents_num, 
		float target_fpr);

/*
 * Function:bf_parameter_free
 * -------------------------
 *		deallocated bloom_filter_meta
 *
 *	bfm:
 * */
void bf_parameter_free(bloom_filter_meta *bfm);

/*
 * Function: bf_set
 * ---------------
 *		setting bloom_filter 
 *
 *	bfm: bloom_filter_meta for bf
 *	bf: target bloom_filter
 *  lba: target lba
 * */
void bf_set(bloom_filter_meta *bfm, bloom_filter *bf, uint32_t lba);

/*
 * Function: bf_check
 * ------------------
 *		querying whether the lba is in bf or not
 * 
 * bfm: testing bloom_filter meta
 * bf: target bloom_filter
 * lba: target lba
 *
 * */
bool bf_check(bloom_filter_meta *bfm, bloom_filter *bf, uint32_t lba);

double get_number_of_bits(double target_fpr);
double get_target_each_fpr(uint32_t member_num, float block_fpr);
/*
uint32_t bf_bit_per_entry(bloom_filter_meta *bfm, 
		float fpr, uint32_t bit_for_psa);
*/
#endif
