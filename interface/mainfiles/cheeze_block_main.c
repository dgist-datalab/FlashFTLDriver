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
#include "../cheeze_block.h"

void log_print(int sig){
	free_cheeze();
	inf_free();
	exit(1);
}

int main(int argc,char* argv[]){
	struct sigaction sa;
	sa.sa_handler = log_print;
	sigaction(SIGINT, &sa, NULL);
	printf("signal add!\n");

	inf_init(1,0,argc,argv);

	vec_request *req=NULL;
	while((req=get_vectored_request())){
		assign_vectored_req(req);
	}

	free_cheeze();
	inf_free();
	return 0;
}
