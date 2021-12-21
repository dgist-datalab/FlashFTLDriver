#include "block_table.h"
#include "sorted_table.h"
#include "page_aligner.h"
#include "summary_page.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./mapping_function.h"
#include "./run.h"
#include "./shortcut.h"
#include "./compaction.h"

uint32_t argument_set_temp(int ,char **){return 1;}
uint32_t operation_temp(request *const ){return 1;}
uint32_t print_log_temp(){return 1;}

uint32_t create_temp(lower_info *,blockmanager *, struct algorithm *);
void destroy_temp(lower_info *, struct algorithm *);
uint32_t write_temp(request *const );
uint32_t read_temp(request *const );
uint32_t test_function();

struct algorithm layered_lsm={
	.argument_set=argument_set_temp,
	.create=create_temp,
	.destroy=destroy_temp,
	.read=read_temp,
	.write=write_temp,
	.flush=operation_temp,
	.remove=operation_temp,
	.test=test_function,
	.print_log=print_log_temp,
};

pp_buffer *page_aligner;
L2P_bm *bm;
lower_info* g_li;
uint32_t run_num=16;
uint32_t entry_per_run;
uint32_t entry_num_last_run;
run **run_array;
sc_master *shortcut;

char all_set_data[PAGESIZE];

uint32_t create_temp(lower_info *li,blockmanager *sm, struct algorithm *){
	page_aligner=pp_init();
	bm=L2PBm_init(sm);
	g_li=li;

	memset(all_set_data, -1, PAGESIZE);
	entry_per_run=RANGE/run_num;
	entry_num_last_run=(RANGE%run_num?RANGE%run_num:entry_per_run);
	run_array=(run**)calloc(run_num, sizeof(run *));
	shortcut=shortcut_init(run_num/2-2, (uint32_t)RANGE);

	sorted_array_master_init();
	return 1;
}

void destroy_temp(lower_info *, struct algorithm *){
	for(uint32_t i=0; i<run_num; i++){
		if(run_array[i]){
			run_free(run_array[i], shortcut);
		}
	}
	sorted_array_master_free();
	shortcut_free(shortcut);
	free(run_array);
	L2PBm_free(bm);
}

static inline uint32_t empty_space(){
	for(uint32_t i=0; i<run_num; i++){
		if(run_array[i]==NULL) return i;
	}
	EPRINT("error??", true);
	return UINT32_MAX;
}

uint32_t write_temp(request *const req){
	static bool debug_flag=false;
	static run *now_run=NULL;
	
	if(now_run==NULL){
		uint32_t idx=empty_space();
		run_array[idx]=run_factory(TREE_MAP, entry_per_run, 0.1, bm, RUN_NORMAL);
		now_run=run_array[idx];
	}

	if(run_is_empty(now_run)){
		shortcut_add_run(shortcut, now_run);
	}

	run_insert(now_run, req->key, UINT32_MAX, req->value->value, false);
	
	if(run_is_full(now_run)){
		run_insert_done(now_run, false);
		if(shortcut_compaction_trigger(shortcut)){
			run *res=compaction_test(shortcut, 2, GUARD_BF, 0.1, bm);
			uint32_t idx=empty_space();
			run_array[idx]=res;
		}
		now_run=NULL;
	}
	req->end_req(req);
	return 1;
}

char *buffer_checker(request *req){
	return pp_find_value(page_aligner, req->key);
}

uint32_t read_temp(request *const req){
	uint32_t res;
	run *t_run=shortcut_query(shortcut, req->key);

	if(!req->retry){
		res=run_query(t_run, req);
	}
	else{
		res=run_query_retry(t_run, req);
	}

	if(res==NOT_FOUND){
		req->type==FS_NOTFOUND_T;
		printf("%u not found\n", req->key);
		abort();
	}
	return 1;
}

uint32_t test_function(){
	return 1;
}
