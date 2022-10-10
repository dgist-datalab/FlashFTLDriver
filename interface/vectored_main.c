#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>
#include "../include/FS.h"
#include "../include/settings.h"
#include "../include/types.h"
#include "../bench/bench.h"
#include "interface.h"
#include "vectored_interface.h"
#include "../include/utils/kvssd.h"
extern int req_cnt_test;
extern uint64_t dm_intr_cnt;
extern int LOCALITY;
extern float TARGETRATIO;
extern int KEYLENGTH;
extern int VALUESIZE;
extern uint32_t INPUTREQNUM;
extern master *_master;
extern bool force_write_start;
extern int seq_padding_opt;
MeasureTime write_opt_time[11];
extern master_processor mp;
extern uint64_t cumulative_type_cnt[LREQ_TYPE_NUM];

int main(int argc,char* argv[]){
	//int temp_cnt=bench_set_params(argc,argv,temp_argv);
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	bench_parameters* bp=bench_parsing_parameters(&argc,argv);

	if(bp){
		inf_init(0,0,argc,argv);
		bench_init();
		bench_vectored_configure();
		for(int i=0; i<bp->max_bench_num; i++){
			bench_meta *bpv=&bp->bench_list[i];
			bench_add(bpv->type,bpv->start, bpv->end, bpv->number);
		}
	}
	else{
		inf_init(0,0,argc,argv);
#if 0
		/*test function code start*/
		inf_algorithm_testing();
		inf_free();
		return 0;
#else 
		bench_init();
		bench_vectored_configure();
	//	bench_add(VECTOREDSSET,0,RANGE,RANGE);
	//	bench_add(VECTOREDSGET,0,RANGE,RANGE);
	//	bench_add(VECTOREDRSET,0,RANGE,RANGE);
	//	inf_print_log();
	//	bench_add(VECTOREDSGET,0, RANGE, RANGE);
	//	bench_add(VECTOREDUNIQRSET,0,RANGE,RANGE);
	//	bench_add(VECTOREDRGET,0, RANGE, RANGE);
		bench_add(VECTOREDRW,0, RANGE,(RANGE)*2);
	//	bench_add(VECTOREDRGET,0,RANGE,RANGE);
	//	bench_add(VECTOREDLOCALIZEDGET,0,RANGE,RANGE);
	//	bench_add(VECTOREDRGET,0,RANGE,RANGE);
	//	bench_add(VECTOREDRSET,0,RANGE,RANGE*2);
#endif
	}
	printf("range: %lu!\n",RANGE);

	char *value;
	uint32_t mark;
	while((value=get_vectored_bench(&mark))){
		inf_vector_make_req(value, bench_transaction_end_req, mark);
	}

	force_write_start=true;
	
	printf("bench finish\n");
	while(!bench_is_finish()){
#ifdef LEAKCHECK
		sleep(1);
#endif
	}

	if(bp){
		bench_parameters_free(bp);
	}
	bench_custom_print(write_opt_time,11);
	
		inf_free();
	return 0;
}
