#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <getopt.h>
#include "../include/lsm_settings.h"
#include "../include/FS.h"
#include "../include/settings.h"
#include "../include/types.h"
#include "../bench/bench.h"
#include "interface.h"
#include "vectored_interface.h"
#include "../algorithm/Lsmtree/lsmtree.h"
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
extern lsmtree LSM;
extern int v_cnt[NPCINPAGE+1];
#ifdef Lsmtree
int skiplist_hit;
#endif
MeasureTime write_opt_time[11];
extern master_processor mp;
extern uint64_t cumulative_type_cnt[LREQ_TYPE_NUM];
int main(int argc,char* argv[]){
	char *temp_argv[10];
	int temp_cnt=bench_set_params(argc,argv,temp_argv);
	inf_init(0,0,temp_cnt,temp_argv);
	bench_init();
	bench_vectored_configure();
	bench_transaction_configure(4, 2);
	printf("TOTALKEYNUM: %ld\n",TOTALKEYNUM);
	bench_add(VECTOREDSET,0,(INPUTREQNUM?INPUTREQNUM:SHOWINGFULL)/1,((INPUTREQNUM?INPUTREQNUM:SHOWINGFULL)));

	char *value;
	uint32_t mark;
	while((value=get_vectored_bench(&mark, true))){
		inf_vector_make_req(value, bench_transaction_end_req, mark);
	}

	force_write_start=true;
	
	printf("bench finish\n");
	while(!bench_is_finish()){
#ifdef LEAKCHECK
		sleep(1);
#endif
	}

	inf_free();
	bench_custom_print(write_opt_time,11);
	return 0;
}
