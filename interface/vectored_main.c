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
#include <sched.h>

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

static int utilization=100;
static int round=1;
static int type=-1;
static int param=0;

int main(int argc,char* argv[]){

	uint32_t cpu_number=1;
	cpu_set_t cpuset;
	pthread_t thread = pthread_self();
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_number, &cpuset);

	int result=pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	for(int i=1; i<argc; i++){
		if(argv[i][0]=='-' && argv[i][1]=='U'){
			utilization=atoi(&argv[i+1][0]);
			for(int j=i; j<argc-2; j++){
				argv[j]=argv[j+2];
			}
			argc-=2;
			i-=2;
		}
		else if(argv[i][0]=='-' && argv[i][1]=='R'){
			round=atoi(&argv[i+1][0]);
			for(int j=i; j<argc-2; j++){
				argv[j]=argv[j+2];
			}
			argc-=2;
			i-=2;
		}
		else if(argv[i][0]=='-' && argv[i][1]=='T'){
			type=atoi(&argv[i+1][0]);
			for(int j=i; j<argc-2; j++){
				argv[j]=argv[j+2];
			}
			argc-=2;
			i-=2;
		}
		else if(argv[i][0]=='-' && argv[i][1]=='P'){
			param=atoi(&argv[i+1][0]);
			for(int j=i; j<argc-2; j++){
				argv[j]=argv[j+2];
			}
			argc-=2;
			i-=2;
		}
	}

	printf("U:%u R:%u T:%d P:%u N:%u\n", utilization, round, type, param, RANGE/100*utilization);


	inf_init(0,0,argc,argv);

	uint32_t target_range_number=RANGE/100*utilization;
	bench_init();
	bench_vectored_configure();
	switch (type)
	{
		case 0:
		case -1: //Random WRITE
			bench_add(VECTOREDUNIQRSET,0,target_range_number, target_range_number, 0);
			bench_add(VECTOREDRSET,0,target_range_number,target_range_number, 0);
			break;
		case 1: //RANDOM read
			bench_add(VECTOREDUNIQRSET,0,target_range_number, target_range_number, 0);
			bench_add(VECTOREDRGET,0,target_range_number,target_range_number, 0);
			break;

		case 2: //Temporal locality RW
			bench_add(VECTOREDUNIQRSET,0,target_range_number,target_range_number*round, param);
			bench_add(VECTOREDTEMPLOCALRW,0,target_range_number,target_range_number*round, param);
			break;

		case 3: //Spatial locality RW
			bench_add(VECTOREDSPATIALRW,0,target_range_number,target_range_number*round, param);
			break;
		case 4: //Random RW
			bench_add(VECTOREDUNIQRSET,0,target_range_number, target_range_number, 0);
			break;
		default:
			printf("type error\n");

			break;
	}


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
	bench_custom_print(write_opt_time,11);

	inf_free();
	return 0;
}
