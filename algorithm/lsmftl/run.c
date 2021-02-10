#include "run.h"

run *run_init(uint32_t pair_num, uint32_t sst_file_num, uint32_t start_lba, uint32_t end_lba){
	run *res=(run*)malloc(sizeof(run));
	res->pair_num=pair_num;
	res->sst_file_num=map_num;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->sst_set=(sst_file*)calloc(sst_file_num, sizeof(sst_file));
	return res;
}

void run_append_sstfile(run *_run, sst_file *sstfile){
	sst_copy(LAST_SST_PTR(_run), sstfile);
}

void run_deep_append_sstfile(run *_run, sst_file *sstfile){
	sst_deep_copy(LAST_SST_PTR(_run), sstfile);
}

void run_free(run *_run){
	free(_run->sst_set);
	free(_run);
}
