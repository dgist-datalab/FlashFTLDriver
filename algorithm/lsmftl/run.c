#include "run.h"

run *run_init(uint32_t sst_file_num, uint32_t start_lba, uint32_t end_lba){
	run *res=(run*)malloc(sizeof(run));
	res->sst_file_num=sst_file_num;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->sst_set=(sst_file*)calloc(sst_file_num, sizeof(sst_file));
	return res;
}

void run_space_init(run *res, uint32_t map_num, uint32_t start_lba, uint32_t end_lba){
	res->sst_file_num=map_num;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->sst_set=(sst_file*)calloc(map_num, sizeof(sst_file));
}

static inline void update_range(run *_run, uint32_t start, uint32_t end){
	if(_run->start_lba > start) _run->start_lba=start;
	if(_run->end_lba < end) _run->end_lba=end;
}

void run_append_sstfile(run *_run, sst_file *sstfile){
	sst_copy(LAST_SST_PTR(_run), sstfile);
	update_range(_run, sstfile->start_lba, sstfile->end_lba);
}

void run_deep_append_sstfile(run *_run, sst_file *sstfile){
	sst_deep_copy(LAST_SST_PTR(_run), sstfile);
	update_range(_run, sstfile->start_lba, sstfile->end_lba);
}

void run_free(run *_run){
	free(_run->sst_set);
	free(_run);
}
