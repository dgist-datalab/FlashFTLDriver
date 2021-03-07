#ifndef __RUN_H__
#define __RUN_H__
#include "../../include/settings.h"
#include "sst_file.h"
#include <stdlib.h>
/*
 d=data
 m=map
 d(start_data_physical_address)-d-d-d-d-d-...-d-m-m...
 */
typedef struct run{
	uint32_t now_sst_file_num;
	uint32_t max_sst_file_num;
	uint32_t start_lba;
	uint32_t end_lba;
	uint32_t start_data_physical_address;
	sst_file *sst_set;
}run;
/*
static inline uint32_t run_file_size(run *r){
	return r->pair_num*LPAGESIZE+r->map_num*PAGESIZE;
}
*/
#define LAST_SST_PTR(run_ptr) (&run_ptr->sst_set[run_ptr->now_sst_file_num-1])
#define FIRST_SST_PTR(run_ptr) (&run_ptr->sst_set[0])

#define for_each_sst(run_ptr, sst_ptr, idx)\
	for(idx=0, (sst_ptr)=&(run_ptr)->sst_set[0]; idx<(run_ptr)->now_sst_file_num; \
			idx++, (sst_ptr)=&(run_ptr)->sst_set[idx])

#define for_each_sst_at(run_ptr, sst_ptr, idx)\
	for((sst_ptr)=&(run_ptr)->sst_set[idx]; idx<(run_ptr)->now_sst_file_num; \
			idx++, (sst_ptr)=&(run_ptr)->sst_set[idx])

run *run_init(uint32_t sst_file_num, uint32_t start_lba, uint32_t end_lba);
sst_file *run_retrieve_sst(run *r, uint32_t lba);
sst_file *run_retrieve_close_sst(run *r, uint32_t lba);
void run_space_init(run *,uint32_t map_num, uint32_t start_lba, uint32_t end_lba);
void run_append_sstfile(run *_run, sst_file *sstfile);
void run_deep_append_sstfile(run *_run, sst_file *sstfile);
void run_free(run *_run);
static inline void run_destroy_content(run *_run){
	sst_file *sptr; uint32_t sidx;
	for_each_sst(_run, sptr, sidx){
		sst_destroy_content(sptr);
	}
	free(_run->sst_set);
}

static inline void run_deep_copy(run *des, run *src){
	sst_file *des_file=des->sst_set;
	*des=*src;
	sst_file *each_src_file;
	uint32_t idx=0;
	for_each_sst(src, each_src_file, idx){
		sst_deep_copy(&des_file[idx], each_src_file);
	}
	des->sst_set=des_file;
}

static inline void run_copy(run *des, run *src){
	*des=*src;
}

static inline void run_copy_src_empty(run *des ,run *src){
	sst_file *temp_file=des->sst_set;
	*des=*src;
	memcpy(temp_file, src->sst_set, sizeof(sst_file)*src->now_sst_file_num);
	memset(src->sst_set, 0 ,sizeof(sst_file)*src->now_sst_file_num);
	src->now_sst_file_num=0;
	des->sst_set=temp_file;
}
#endif
