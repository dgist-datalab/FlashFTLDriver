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

int			serversock, accp_sock;

typedef struct recv_data_t{
	int num[2];
	int offset[2];
}r_data;

typedef struct s_data_pointer_t{
	int *table_num;
	int *offset;
	float *value;
}s_data_p;


typedef struct s_data_t{
	int table_num;
	int offset;
	float value[ENTRY_NUM];
}s_data;

int cnt = 0;
float batch_emb[BATCH][TABLE_NUM][ENTRY_NUM];
float sum_emb[BATCH][ENTRY_NUM];
int table_addr[TABLE_NUM+1];

void quit(char* msg,int retval){
	if (retval == 0) {
		fprintf(stdout, (msg == NULL ?"" : msg));
		fprintf(stdout,"\n");
	}else {
		fprintf(stderr, (msg == NULL ?"" : msg));
		fprintf(stderr,"\n");
	}

	if(serversock) close(serversock);

	exit(retval);
}
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
	s_data_p *s_p = (s_data_p*)req;
	s_data * my = (s_data*)malloc(sizeof(s_data));

	/*
	my->table_num = *(s_p->table_num);
	my->offset = *(s_p->offset);
	memcpy(my->value, s_p->value, P_SIZE); 
	*/
	int idx;
	my->table_num = *(s_p->table_num);
	my->offset = *(s_p->offset);

	idx = ((my->offset*ENTRY_NUM*sizeof(float)) % P_SIZE)/4;
	memcpy(my->value, &(s_p->value[idx]), ENTRY_NUM*sizeof(float)); 

	// send data
	int bytes = send(accp_sock, my, sizeof(s_data), 0);

	if(bytes != sizeof(s_data)){
		fprintf(stderr,"Connection closed. bytes->[%d], dataSize->[%d]\n",bytes, sizeof(s_data));
		close(accp_sock);

		if ((accp_sock = accept(serversock, NULL, NULL)) == -1) {
			quit("accept() failed", 1);
		}
	}	

	free(my);
}

void write_init(){
	// write weight data into SSD
	char* in = (char*)malloc(sizeof(char)*P_SIZE);
	char numb[5];
	char dir[100];
	char last[] = ".weight.txt";
	int fd=0;
	int read_size=0;
	uint32_t offset=0;

	for(int i=0; i<TABLE_NUM; ++i){
//	for(int i=0; i<2; ++i){
		memset(dir, 0, sizeof(dir));
		strcpy(dir, "./local_disk/save_file/emb_l.");
		sprintf(numb, "%ld", i);
		strcat(dir, numb);
		strcat(dir, last);
		read_size =0;
		offset=0;

		if( (fd = open(dir, O_RDONLY)) == -1){
			printf("open erro\n");
			printf("%s\n",dir);
			exit(-1);
		}
		while( 0 < (read_size = read(fd, in, P_SIZE))) {
			inf_make_req(FS_SET_T, table_addr[i]+offset ,in ,P_SIZE,0);
			offset += 1;
		}
		table_addr[i+1] = offset;
		printf("%s done!\n", dir);
	}
	printf("write all weight file to SSD!\n");
}


void network_init(){

	struct sockaddr_in      server, client;
	unsigned int addrlen = sizeof(client);

	/* open socket */
	if ((serversock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		quit("socket() failed", 1);
	}

	/* setup server's IP and port */
	memset(&server, 0,sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);
	server.sin_addr.s_addr = INADDR_ANY;

	/* bind the socket */
	if (bind(serversock, (struct sockaddr*)&server,sizeof(server)) == -1) {
		quit("bind() failed", 1);
	}

	printf("start listen....\n");
	// wait for connection
	if(listen(serversock, 10) == -1){
		quit("listen() failed.", 1);
	}
	printf("client wait....\n");

	accp_sock = accept(serversock, (struct sockaddr *)&client, &addrlen);
	if(accp_sock < 0){
		quit("accept() failed", 1);
	}

}

int recevie_data_from_dlrm(r_data * r_value){
	//1부터 시작하기때문에 0부터 시작하게 바꿔줌
	r_value->offset[0] -= 1;

	int bytes = 0;
	for(int i=0; i<sizeof(r_data); i+= bytes){
		if ((bytes = recv(accp_sock, r_value + i,sizeof(r_data) - i, 0)) == -1) {
			quit("recv failed", 1);
		}
	}
	//printf("%d ", table_addr[r_value->num[0]]);
	return table_addr[r_value->num[0]] + (r_value->offset[0]*ENTRY_NUM*sizeof(float))/P_SIZE;
}

int main(int argc,char* argv[]){

	inf_init(0,0,0,NULL);
	bench_init();
	network_init();
	write_init();

	r_data* r_value = (r_data*) malloc(sizeof(r_data));
	while(1){

		int addr = recevie_data_from_dlrm(r_value);

		inf_make_req_special(FS_GET_T,addr,NULL ,P_SIZE,0,r_value->num[0], r_value->offset[0], main_end_req);

	}

	inf_free();
	return 0;
}
