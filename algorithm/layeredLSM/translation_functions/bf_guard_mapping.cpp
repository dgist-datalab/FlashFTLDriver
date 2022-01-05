#include "bf_guard_mapping.h"
#include "../../../include/search_template.h"

float __target_fpr;
uint32_t __target_member;
static bloom_filter_meta * __global_bfm;

float find_sub_member_num(float fpr, uint32_t member, uint32_t lba_bit_num){
	static float target_avg_bit=0;
	if(target_avg_bit) return target_avg_bit;
	for(uint32_t i=2; i<member/2; i++){
//		uint32_t member_set_num=member/i;
		float target_each_fpr=get_target_each_fpr(i, fpr);
		uint32_t bit=get_number_of_bits(target_each_fpr);
		if(bit==0) continue;
		float avg_bit=(float)(bit*i+lba_bit_num)/i;
		if(target_avg_bit==0){
			target_avg_bit=avg_bit;
		}
		else if(target_avg_bit > avg_bit){
			__target_fpr=target_each_fpr;
			__target_member=i;
			target_avg_bit=avg_bit;
			printf("%u -> %.2f\n", __target_member, target_avg_bit);
		}
	}
	__global_bfm=bf_parameter_setting(__target_member, fpr);
	return target_avg_bit;
}

map_function *	bfg_map_init(uint32_t contents_num, float fpr, uint32_t bit){
	if(__target_fpr==0){
		find_sub_member_num(fpr, contents_num, bit);
	}
	map_function *res=(map_function*)calloc(1, sizeof(map_function));

	res->insert=bfg_map_insert;
	res->query=bfg_map_query;
	res->oob_check=map_default_oob_check;
	res->query_retry=bfg_map_query_retry;
	res->query_done=map_default_query_done;
	res->make_done=bfg_map_make_done;
	res->show_info=NULL;
	res->get_memory_usage=bfg_get_memory_usage;
	res->free=bfg_map_free;

	uint32_t set_num=(contents_num/__target_member)+(contents_num%__target_member?1:0);
	bfg_map *map=(bfg_map*)malloc(sizeof(bfg_map));
	//map->bfm=__global_bfm;
	map->bf_set=(bf_map*)malloc(sizeof(bf_map) * set_num);
	map->guard_set=(uint32_t*)malloc(sizeof(uint32_t) * (set_num+1));
	memset(map->guard_set, -1, sizeof(uint32_t) * (set_num+1));

	for(uint32_t i=0; i<set_num; i++){
		map->bf_set[i].set_of_bf=(bloom_filter*)malloc(sizeof(bloom_filter) *__target_member);
		map->bf_set[i].write_pointer=0;
	}
	map->write_pointer=0;

	res->private_data=map;
	return res;
}

uint32_t			bfg_map_insert(map_function *mf, uint32_t lba, uint32_t offset){
	bfg_map *map=extract_bfg_map(mf);
	if(map_full_check(mf)){
		EPRINT("data overflow", true);
	}
	uint32_t now_write_set_ptr=map->write_pointer/__target_member;
	if(map->bf_set[now_write_set_ptr].write_pointer==0){
		map->guard_set[now_write_set_ptr]=lba;
	}
	bf_map *t_map=&map->bf_set[now_write_set_ptr];
	bf_set(__global_bfm, &t_map->set_of_bf[t_map->write_pointer++], lba);
	map->write_pointer++;
	map_increase_contents_num(mf);
	return INSERT_SUCCESS;
}

uint64_t 		bfg_get_memory_usage(map_function *mf, uint32_t target_bit){
	uint64_t res=0;
	bfg_map *map=extract_bfg_map(mf);
	uint32_t now_write_set_ptr=map->write_pointer/__target_member;
	res+=__global_bfm->bits*mf->now_contents_num;
	res+=target_bit * now_write_set_ptr;
	return res;
}

int bfg_cmp(uint32_t b, uint32_t target){return b-target;}

uint32_t		bfg_map_query(map_function *mf, uint32_t lba, map_read_param **param){
	map_read_param *res_param=(map_read_param*)malloc(sizeof(map_read_param));
	res_param->lba=lba;
	res_param->mf=mf;
	res_param->oob_set=NULL;
	res_param->private_data=NULL;
	*param=res_param;

	uint32_t target_set_idx;
	bfg_map *map=extract_bfg_map(mf);
	bs_lower_bound(map->guard_set, 0, CEIL(map->write_pointer, __target_member)-1, lba, bfg_cmp, target_set_idx);

	if(map->guard_set[target_set_idx] > lba){
		target_set_idx--;
	}

	if(target_set_idx==UINT32_MAX){
		return NOT_FOUND;
	}
	res_param->prev_offset=target_set_idx*__target_member;
	bf_map *t_map=&map->bf_set[target_set_idx];
	for(uint32_t i=0; i<t_map->write_pointer; i++){
		if(bf_check(__global_bfm, &t_map->set_of_bf[i], lba)){
			res_param->prev_offset+=i;
			return res_param->prev_offset;
		}
	}
	return NOT_FOUND;
}


uint32_t		bfg_map_query_retry(map_function *mf, map_read_param *param){
	bfg_map *map=extract_bfg_map(mf);
	if(param->prev_offset >= map->write_pointer) return NOT_FOUND;
	uint32_t lba=param->lba;
	uint32_t target_set_idx=param->prev_offset/__target_member;
	bf_map *t_map=&map->bf_set[target_set_idx];
	param->prev_offset++;
	for(uint32_t i=param->prev_offset%__target_member; i<t_map->write_pointer; 
			i++, param->prev_offset++){
		if(bf_check(__global_bfm, &t_map->set_of_bf[i], lba)){
			return param->prev_offset;
		}
	}
	return NOT_FOUND;
}

void			bfg_map_make_done(map_function *mf){
	return;
}

void			bfg_map_free(map_function *mf){
	bfg_map *map=extract_bfg_map(mf);
	for(uint32_t i=0; i<CEIL(mf->max_contents_num, __target_member); i++){
		free(map->bf_set[i].set_of_bf);
	}
	free(map->guard_set);
	free(map->bf_set);
	free(map);
	free(mf);
}

