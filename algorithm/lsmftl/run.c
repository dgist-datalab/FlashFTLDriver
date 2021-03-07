#include "run.h"

run *run_init(uint32_t sst_file_num, uint32_t start_lba, uint32_t end_lba){
	run *res=(run*)malloc(sizeof(run));
	res->now_sst_file_num=0;
	res->max_sst_file_num=sst_file_num;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->sst_set=(sst_file*)calloc(sst_file_num, sizeof(sst_file));
	return res;
}

void run_space_init(run *res, uint32_t map_num, uint32_t start_lba, uint32_t end_lba){
	res->max_sst_file_num=map_num;
	res->now_sst_file_num=0;
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->sst_set=(sst_file*)calloc(map_num, sizeof(sst_file));
}

static inline void update_range(run *_run, uint32_t start, uint32_t end){
	if(_run->start_lba > start) _run->start_lba=start;
	if(_run->end_lba < end) _run->end_lba=end;
}


sst_file *run_retrieve_sst(run *r, uint32_t lba){
	int s=0, e=r->now_sst_file_num-1;
	while(s<=e){
		int mid=(s+e)/2;
		if(r->sst_set[mid].start_lba<=lba && r->sst_set[mid].end_lba>=lba){
			return &r->sst_set[mid];
		}
		if(r->sst_set[mid].start_lba > lba){
			e=mid-1;
		}
		else if(r->sst_set[mid].end_lba < lba){
			s=mid+1;
		}
	}
	return NULL;
}

sst_file *run_retrieve_close_sst(run *r, uint32_t lba){
	int s=0, e=r->now_sst_file_num-1;
	int mid;
	bool is_right=false;
	while(s<=e){
		mid=(s+e)/2;
		if(r->sst_set[mid].start_lba<=lba && r->sst_set[mid].end_lba>=lba){
			EPRINT("overlap not allowed",true);
			return NULL;
		}
		if(r->sst_set[mid].start_lba > lba){
			e=mid-1;
			is_right=false;
		}
		else if(r->sst_set[mid].end_lba < lba){
			s=mid+1;
			is_right=true;
		}
	}
	
	if(is_right) return &r->sst_set[mid];
	else{ 
		return mid-1<0? NULL:&r->sst_set[mid-1];
	}
}

void run_append_sstfile(run *_run, sst_file *sstfile){
	if(_run->now_sst_file_num>_run->max_sst_file_num){
		EPRINT("over sst file", true);
	}
	if(_run->now_sst_file_num && !(sstfile->start_lba > _run->start_lba && sstfile->start_lba>_run->end_lba)){
		EPRINT("cannot append!", true);
	}
	sst_deep_copy(&_run->sst_set[_run->now_sst_file_num], sstfile);
	update_range(_run, sstfile->start_lba, sstfile->end_lba);
	_run->now_sst_file_num++;
}

void run_deep_append_sstfile(run *_run, sst_file *sstfile){
	if(_run->now_sst_file_num>_run->max_sst_file_num){
		EPRINT("over sst file", true);
	}
	if(_run->now_sst_file_num && !(sstfile->start_lba > _run->start_lba && sstfile->start_lba>_run->end_lba)){
		EPRINT("cannot append!", true);
	}
	sst_deep_copy(&_run->sst_set[_run->now_sst_file_num], sstfile);
	update_range(_run, sstfile->start_lba, sstfile->end_lba);
	_run->now_sst_file_num++;
}

void run_free(run *_run){
	free(_run->sst_set);
	free(_run);
}
