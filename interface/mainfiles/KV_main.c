#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#include "../include/settings.h"
#include "../bench/bench.h"
#include "interface.h"
#include "queue.h"

typedef struct netdata_t{
	uint8_t type;
	uint8_t keylen;
	uint32_t seq;
	uint32_t scanlength;
	char key[UINT8_MAX];
}netdata;
MeasureTime temp;
int client_socket;
int input_num, send_num;
queue *n_q;

MeasureTime write_opt_time[10];
void log_print(int sig){
//	while(!bench_is_finish()){}
	inf_free();
	exit(1);
}

int server_socket;
struct sockaddr_in client_addr;
socklen_t client_addr_size;

void read_socket_len(char *buf, int len){
	int readed=0, temp_len;
	while(len!=readed){
		if((temp_len=read(client_socket,buf,len-readed))<0){
			if(errno==11) continue;
			printf("errno: %d temp_len:%d\n",errno,temp_len);
			printf("length read error!\n");
			abort();
		}else if(temp_len==0){
			printf("connection closed :%d\n", errno);
			client_socket     = accept( server_socket, (struct sockaddr*)&client_addr, &client_addr_size);

		}
		readed+=temp_len;
	}
}

void write_socket_len(char *buf, int len){
	int wrote=0, temp_len;
	while(len!=wrote){
		if((temp_len=write(client_socket,buf,len-wrote))<0){
			if(errno==11) continue;
			printf("errno: %d\n",errno);
			printf("length write error!\n");
			abort();
		}
		wrote+=temp_len;
	}
}

void print_byte(char *data, int len){
	for(int i=0; i<len; i++){
		printf("%d ",data[i]);
	}
	printf("\n");
}

void *ack_to_client(void *arg){
	netdata *net_data;
	while(1){
		void *req;
		req=q_dequeue(n_q);
		if(!req) continue;
		net_data=(netdata*)req;
		//printf("write:");
		//print_byte((char*)&net_data->seq,sizeof(net_data->seq));

		write_socket_len((char*)&net_data->seq,sizeof(net_data->seq));
		//	if(net_data->type==2){
		free(net_data);
		//	}
		send_num++;
		/*
		if(send_num%100==0){
			printf("%d - %d \n",input_num,send_num);
		}*/
	}
}

void kv_main_end_req(uint32_t a, uint32_t b, void *req){
	if(req==NULL) return;
	netdata *net_data=(netdata*)req;
	//	net_data->seq=a;
	switch(net_data->type){
		case FS_RANGEGET_T:
		case FS_GET_T:
			//printf("insert_queue\n");
			while(!q_enqueue((void*)net_data,n_q));
			//		printf("assign seq:%d\n",a);
			break;
		case FS_SET_T:
			//		free(net_data);
			break;
	}
}
int main(){
	struct sigaction sa;
	sa.sa_handler = log_print;
	sigaction(SIGINT, &sa, NULL);
	printf("signal add!\n");

	q_init(&n_q,128);

	struct sockaddr_in server_addr;

	server_socket = socket(PF_INET,SOCK_STREAM,0);
	int option = 1;
	setsockopt( server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option) );

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family     = AF_INET;
	server_addr.sin_port       = htons(PORT);
	server_addr.sin_addr.s_addr= inet_addr(IP);

	if(-1 == bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr))){
		printf("bind error!\n");
		exit(1);
	}

	if(-1 == listen(server_socket, 5)){
		exit(1);
	}

	pthread_t t_id;
	pthread_create(&t_id,NULL,ack_to_client,NULL);
	inf_init(1,0);

	printf("server ip:%s port:%d\n",IP,PORT);
	printf("server waiting.....\n");
	client_addr_size  = sizeof( client_addr);
	client_socket     = accept( server_socket, (struct sockaddr*)&client_addr, &client_addr_size);
	printf("connected!!!\n");
	if (-1 == client_socket){
		exit(1);
	}

	int flags=fcntl(client_socket,F_GETFL,0);
	if(flags & O_NONBLOCK){
		printf("non blocking!\n");
	}
	else{
		printf("blocking socket!\n");
	}

	netdata *data;
	char temp[8192]={0,};
	char data_temp[6];
	data=(netdata*)malloc(sizeof(netdata));
	//measure_init(&data->temp);
	while(1){
		read_socket_len((char*)data_temp,sizeof(data->keylen)+sizeof(data->type)+sizeof(data->seq));
		data->type=data_temp[0];
		data->keylen=data_temp[1];
		data->seq=*(uint32_t*)&data_temp[2];
		if(data->type==3){
			read_socket_len((char*)&data->scanlength,sizeof(data->scanlength));
			data->scanlength=htobe32(data->scanlength);
		//	data->type=FS_GET_T;
			data->type=FS_RANGEGET_T;
			read_socket_len(data->key,data->keylen);
	//		fprintf(stderr,"%d %d %d %.*s\n",3,data->scanlength,data->keylen,data->keylen,data->key);
			inf_make_range_query_apps(data->type,data->key,data->keylen,data->seq,data->scanlength,data,kv_main_end_req);
		}else{
			read_socket_len(data->key,data->keylen);
	//		fprintf(stderr,"%d 0 %d %.*s\n",data->type,data->keylen,data->keylen,data->key);
			inf_make_req_apps(data->type,data->key,data->keylen,temp,PAGESIZE-data->keylen-sizeof(data->keylen),data->seq,data->type==2?data:NULL,kv_main_end_req);
		}
		input_num++;
		if(data->type==1){
			while(!q_enqueue((void*)data,n_q));
		}
		data=(netdata*)malloc(sizeof(netdata));	
	}

	while(!bench_is_finish()){}
}
