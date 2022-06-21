#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include "../../include/FS.h"
#include "../../include/settings.h"
#include "../../include/types.h"
#include "../../bench/bench.h"
#include "../interface.h"
#include "../vectored_interface.h"
#include "../cheeze_hg_block.h"

void log_print(int sig){
	request_print_log();
	request_memset_print_log();
	free_cheeze();
	printf("f cheeze\n");
	inf_free();
	printf("f inf\n");
	fflush(stdout);
	fflush(stderr);
	sync();
	printf("before exit\n");
	exit(1);
}

void log_lower_print(int sig){
    printf("-------------lower print!!!!-------------\n");
    inf_lower_log_print();
    printf("-------------lower print end-------------\n");
}

pthread_t thr2;
void *thread_print(void *){
	inf_print_log();
	return NULL;
}

//int MS_TIME_SL;
extern bool print_read_latency;
void print_temp_log(int sig){
	struct sigaction sa2={0};
	sa2.sa_handler = print_temp_log;
	sigaction(SIGCONT, &sa2, NULL);

	printf("%d\n", pthread_self());
	request_print_log();
	request_memset_print_log();
	//inf_print_log();
	pthread_create(&thr2, NULL, thread_print, NULL);
	pthread_detach(thr2);
//	print_read_latency=tru
}

#ifdef WRITE_STOP_READ
extern fdriver_lock_t write_check_lock;
extern volatile uint32_t write_cnt;
#endif
void * thread_test(void *){
	sigset_t tSigSetMask;
	int nSigNum;
	int nErrno;
	sigemptyset(&tSigSetMask);
//	sigaddset(&tSigSetMask, SIGUSR1);
	pthread_sigmask(SIG_SETMASK, &tSigSetMask, NULL);

	vec_request **req_arr=NULL;
	while((req_arr=get_vectored_request_arr())){
		for(int i=0; req_arr[i]!=NULL; i++){
#ifdef WRITE_STOP_READ
			if (req_arr[i]->type == FS_SET_T){
				fdriver_lock(&write_check_lock);
				//printf("issue cnt:%u %u\n", write_cnt, req_arr[i]->seq_id);
				write_cnt++;
				fdriver_unlock(&write_check_lock);
			}
			else if(req_arr[i]->type==FS_GET_T){	
				while(write_cnt!=0){}
			}
#endif
			assign_vectored_req(req_arr[i]);
		}
		free(req_arr);
	}
	return NULL;
}


pthread_t thr; 
int main(int argc,char* argv[]){
	struct sigaction sa;
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	sa.sa_handler = log_print;
	sigaction(SIGINT, &sa, NULL);

	sigset_t tSigSetMask;
	sigdelset(&tSigSetMask, SIGINT);
	pthread_sigmask(SIG_SETMASK, &tSigSetMask, NULL);

	struct sigaction sa2={0};
	sa2.sa_handler = print_temp_log;
	sigaction(SIGCONT, &sa2, NULL);

	printf("signal add!\n");

//	MS_TIME_SL = atoi(getenv("MS_TIME_SL"));
//	printf("Using MS_TIME_SL of %d\n", MS_TIME_SL);


	inf_init(1,0, argc, argv);
	init_cheeze(0);
/*
	if(argc<2){
		init_cheeze(0);
	}
	else{
		init_cheeze(atoll(argv[1]));
	}
*/
	pthread_create(&thr, NULL, thread_test, NULL);
	pthread_join(thr, NULL);

	free_cheeze();
	inf_free();
	return 0;
}
