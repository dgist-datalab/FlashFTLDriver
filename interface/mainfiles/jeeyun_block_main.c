#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include "../../include/FS.h"
#include "../../include/settings.h"
#include "../../include/types.h"
#include "../../bench/bench.h"
#include "../interface.h"
#include "../vectored_interface.h"
#include "../cheeze_hg_block.h"

void log_print(int sig){
	free_cheeze();
	//printf("f cheeze\n");
	inf_free();
	//printf("f inf\n");
	fflush(stdout);
	fflush(stderr);
	sync();
	//printf("before exit\n");
	exit(1);
}

//char* argv2=NULL;
void * thread_test(void * argv){
	/*
	vec_request **req_arr=NULL;
	while((req_arr=get_vectored_request_arr())){
		for(int i=0; req_arr[i]!=NULL; i++){
			assign_vectored_req(req_arr[i]);
		}
		free(req_arr);
	}
	*/
	FILE *pFile = fopen((char*)argv, "r");
        char tmp[128];

        while(fgets(tmp, 128, pFile)) {
		vec_request *req=jy_ureq2vec_req(tmp);
                if (!req) continue;
		assign_vectored_req(req);
                //free(req_arr);
        }

        fclose(pFile);

	return NULL;
}

void log_lower_print(int sig){
        printf("----------lower print!!!!----------\n");
        inf_lower_log_print();
        printf("----------lower print end----------\n");
}

extern time_t time_begin;
//int MS_TIME_SL;
pthread_t thr; 
int main(int argc,char* argv[]){
	
	struct sigaction sa;
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	sa.sa_handler = log_print;
	sigaction(SIGINT, &sa, NULL);
	//printf("signal add!\n");

//	MS_TIME_SL = atoi(getenv("MS_TIME_SL"));
//	printf("Using MS_TIME_SL of %d\n", MS_TIME_SL);

    struct sigaction sa2;
    sa2.sa_handler = log_lower_print;
    sigaction(SIGUSR1, &sa2, NULL);

    	

	inf_init(1,0, argc, argv, false);

	/*
	if(argc<2){
		init_cheeze(0);
	}
	else{
		init_cheeze(atoll(argv[1]));
	}
	*/
	time_t begin;
	time(&begin);
	time(&time_begin);

        pthread_create(&thr, NULL, thread_test, (void*) argv[1]);
        pthread_join(thr, NULL);
        
	/*
	pthread_create(&thr, NULL, thread_test, (void*) argv[2]);
        pthread_join(thr, NULL);
	*/
	//free_cheeze();

        while(!jy_is_finished()){
#ifdef LEAKCHECK
                sleep(1);
#endif
        }


	time_t end;
	time(&end);
	printf("runtime: %0.0f sec \n", difftime(end, begin));

	//free_cheeze();
	inf_free();
	return 0;
}
