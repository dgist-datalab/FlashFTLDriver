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
#include "../algorithm/Lsmtree/lsmtree.h"
#include "../include/utils/kvssd.h"
extern int req_cnt_test;
extern uint64_t dm_intr_cnt;
extern int LOCALITY;
extern float TARGETRATIO;
extern int KEYLENGTH;
extern int VALUESIZE;
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
	char t_value[PAGESIZE];
	memset(t_value,'x',PAGESIZE);

	printf("TOTALKEYNUM: %ld\n",TOTALKEYNUM);
	
//	bench_add(FILLRAND,0,SHOWINGFULL,SHOWINGFULL);
	bench_add(RANDSET,0,SHOWINGFULL,DEVFULL*5);
//	bench_add(RANDGET,0,SHOWINGFULL,DEVFULL);
	//bench_add(RANDGET,0,SHOWINGFULL,DEVFULL);
	//bench_add(RANDSET,0,RANGE,MAXKEYNUMBER/16); //duplicated test
	//bench_add(RANDGET,0,RANGE,MAXKEYNUMBER/16); //duplicated test
	//bench_add(RANDSET,0,RANGE,REQNUM); ///duplicated test


//	bench_add(RANDRW,0,RANGE,MAXKEYNUMBER/5*2);

//	bench_add(NOR,0,-1,-1);
	bench_value *value;

	value_set temp;
	temp.value=t_value;
	//temp.value=NULL;
	temp.dmatag=-1;
	temp.length=0;

	int locality_check=0,locality_check2=0;
	MeasureTime aaa;
	measure_init(&aaa);
	bool tflag=false;
	uint32_t req_cnt=0;
	while((value=get_bench())){
		temp.length=value->length;
		if(value->type==FS_SET_T){
			memcpy(&temp.value[0],&value->key,sizeof(value->key));
		}
		inf_make_req(value->type,value->key,temp.value ,value->length,value->mark);
#ifdef KVSSD
		free(value->key.key);
#endif
		if(!tflag &&value->type==FS_GET_T){
			tflag=true;
		}

		if(_master->m[_master->n_num].type<=SEQRW) continue;
		
#ifndef KVSSD
		if(value->key<RANGE*TARGETRATIO){
			locality_check++;
		}
		else{
			locality_check2++;
		}
#endif
		req_cnt++;
		if(req_cnt%10000000==0){
			printf("\nlog %d req_cnt\n",req_cnt);
			for(int i=0; i<LREQ_TYPE_NUM;i++){
				fprintf(stderr,"org %s %lu\n",bench_lower_type(i),mp.li->req_type_cnt[i]);
			}
			for(int i=0; i<LREQ_TYPE_NUM;i++){
				fprintf(stderr,"comp %s %lu\n",bench_lower_type(i),cumulative_type_cnt[i]);
			}
			fflush(stderr);
		}
	}

	force_write_start=true;
	
	printf("bench finish\n");
	while(!bench_is_finish()){
#ifdef LEAKCHECK
		sleep(1);
#endif
	}
	//printf("locality: 0~%.0f\n",RANGE*TARGETRATIO);

	printf("bench free\n");
	//LSM.lop->all_print();
	inf_free();
	bench_custom_print(write_opt_time,11);
#ifdef Lsmtree
	printf("skiplist hit:%d\n",skiplist_hit);
#endif
	printf("locality check:%f\n",(float)locality_check/(locality_check+locality_check2));
	/*
	for(int i=0; i<=NPCINPAGE; i++){
		printf("%d - %d\n",i,v_cnt[i]);
	}*/
	return 0;
}
