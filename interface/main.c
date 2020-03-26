#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include "../include/FS.h"
#include "../include/settings.h"
#include "../include/types.h"
#include "../bench/bench.h"
#include "interface.h"
#include "../include/utils/kvssd.h"

#define PORT 9001
#define LEN 256

#define P_SIZE 8192
#define BATCH 64
#define TABLE_NUM 26
#define ENTRY_NUM 256

int	serversock, clientsock;
struct sockaddr_in      server, client;

struct r_data_t{
	int num[2];
	int offset[2];
}r_data;

typedef struct s_data_t{
    int *table_num;
    int *offset;
    float *value;
}s_data;

int cnt = 0;
float batch_emb[BATCH][TABLE_NUM][ENTRY_NUM];
float sum_emb[BATCH][ENTRY_NUM];
uint32_t table_addr[TABLE_NUM+1];

void sum_embedding_table(){
    for(int i=0; i<BATCH; ++i){
	float tmp=0;
	for(int j=0; j<ENTRY_NUM; ++j){
	    for(int k=0; k<TABLE_NUM; ++k){
	    	tmp += batch_emb[i][k][j];
	    }
	    sum_emb[i][j] = tmp;
	}
    }
}

void *main_end_req(void *req){
	if(req==NULL) return NULL;
	s_data *s_data_p = (s_data*)req;

	for(int i=0; i<1024; ++i){
		batch_emb[*(s_data_p->table_num)][*(s_data_p->offset)][i] = s_data_p->value[i];
	}

	/*
	for(int i=0; i<10; i++){
		printf("%f\n", s_data_p->value[i]);
	}
	*/
}

int main(int argc,char* argv[]){
	inf_init(0,0,0,NULL);
	bench_init();
//
//	// network init
//	if ((serversock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
//		quit("socket() failed", 1);
//	}
//
//	/* setup server's IP and port */
//	memset(&server, 0,sizeof(server));
//	server.sin_family = AF_INET;
//	server.sin_port = htons(PORT);
//	server.sin_addr.s_addr = INADDR_ANY;
//
//	/* bind the socket */
//	if (bind(serversock, (struct sockaddr*)&server,sizeof(server)) == -1) {
//		quit("bind() failed", 1);
//	}
//
//	printf("start listen....\n");
//	// wait for connection
//	if(listen(serversock, 10) == -1){
//		quit("listen() failed.", 1);
//	}
//	printf("client wait....\n");
//
//	accp_sock = accept(serversock, (struct sockaddr *)&client, &addrlen);
//	if(accp_sock < 0){
//		quit("accept() failed", 1);
//	}


	char* samp = "hello";
	char* in = (char*)malloc(sizeof(char)*P_SIZE);
	char* out = (char*)malloc(sizeof(char)*P_SIZE);


	strcpy(in, samp);
	char numb[2];
	char dir[100];
	char last[] = ".weight.txt";
	int fd=0;
	int read_size=0;
	uint32_t offset=0;


	for(int i=0; i<TABLE_NUM; ++i){
	    memset(dir, 0, sizeof(dir));
	    strcpy(dir, "./local_disk/save_file/emb_l.");
	    sprintf(numb, "%d", i);
	    strcat(dir, numb);
	    strcat(dir, last);
	    printf("processing %s \n",dir);
	    read_size =0;
	    offset=0;

	    if( (fd = open(dir, O_RDONLY)) == -1){
		printf("open erro\n");
		printf("%s\n",dir);
		return -1;
	    }
	    while( 0 < (read_size = read(fd, in, P_SIZE))) {
		offset += 1;
	    	inf_make_req(FS_SET_T, table_addr[i]+offset ,in ,P_SIZE,0);
	    }
	    table_addr[i+1] = offset;
	}
	for(int i=0; i<TABLE_NUM; ++i){
	    printf("%llu ", table_addr[i]);
	}

//	inf_make_req_special(FS_GET_T,1000,out ,P_SIZE,0,table_num, offset, main_end_req);

	while(1){}
	/*
	printf("in : %s \n", in);
	printf("out : %s \n", out);
	printf("out : %d \n", out[0]);
	printf("out : %d \n", out[1]);
	*/
	//LSM.lop->all_print();


	inf_free();
	return 0;
}
