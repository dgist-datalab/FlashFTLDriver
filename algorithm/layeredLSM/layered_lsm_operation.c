#include "block_table.h"
#include "sorted_table.h"
#include "page_aligner.h"
#include "summary_page.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./mapping_function.h"
#include "./run.h"

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

char all_set_data[PAGESIZE];

uint32_t create_temp(lower_info *li,blockmanager *sm, struct algorithm *){
	page_aligner=pp_init();
	bm=L2PBm_init(sm);
	g_li=li;

	memset(all_set_data, -1, PAGESIZE);
	entry_per_run=RANGE/run_num;
	entry_num_last_run=(RANGE%run_num?RANGE%run_num:entry_per_run);
	run_array=(run**)malloc(sizeof(run *) *run_num);
	for(uint32_t i=0; i<run_num; i++){
		run_array[i]=run_init(EXACT, i==run_num-1?entry_num_last_run:entry_per_run, 0.1, bm);
	}

	return 1;
}

void destroy_temp(lower_info *, struct algorithm *){
	L2PBm_free(bm);
	for(uint32_t i=0; i<run_num; i++){
		printf("run_print %u ------\n", i);
		run_print(run_array[i]);
		run_free(run_array[i]);
	}
	free(run_array);
}

uint32_t write_temp(request *const req){
	static bool debug_flag=false;
	static uint32_t cnt=0;
	uint32_t ridx=cnt++/entry_per_run;
	run_insert(run_array[ridx], req->key, req->value->value);
	
	if(run_is_full(run_array[ridx])){
		run_insert_done(run_array[ridx]);
	}
	req->end_req(req);
	return 1;
}

char *buffer_checker(request *req){
	return pp_find_value(page_aligner, req->key);
}

uint32_t read_temp(request *const req){
	if(!req->retry){
		static uint32_t cnt=0;
		uint32_t ridx=cnt++/entry_per_run;
		run_query(run_array[ridx], req);
	}
	else{
		run_query_retry(run_extract_target(req), req);
	}
	return 1;
}

uint32_t test_function(){
	return 1;
}
