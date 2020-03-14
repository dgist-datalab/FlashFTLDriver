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
#include "../include/lsm_settings.h"
#include "../include/FS.h"
#include "../include/settings.h"
#include "../include/types.h"
#include "../bench/bench.h"
#include "server.h"
#include "interface.h"
#include "queue.h"

#ifdef Lsmtree
int skiplist_hit;
#endif
#define MAX_RET 1024
#define REQSIZE sizeof(net_data_t)
#define PACKETSIZE sizeof(net_data_t)
queue *ret_q;
pthread_mutex_t send_lock;

pthread_mutex_t ret_q_lock;
pthread_cond_t ret_cond;

int client_socket;

void log_print(int sig){
	inf_free();
	exit(1);
}
void *flash_returner(void *param){
	uint32_t req_array[MAX_RET];
	uint32_t writed,len;
	int cnt=0;
	while(1){
		void *req;
		for(int i=0; i<MAX_RET; i++){
			req=q_dequeue(ret_q);
			if(req){
				req_array[cnt++]=*(uint32_t*)req;
				free(req);
			}
			else{
				break;
			}
		}
		if(cnt){
			//printf("cnt:%d\n",cnt);
		}
		writed=0; len=0;
		while(writed!=sizeof(uint32_t)*cnt){
			len=write(client_socket,&((char*)req_array)[writed],sizeof(uint32_t)*cnt-writed);
			if(len){
				writed+=len;
			}
		}
		cnt=0;
	}
	return NULL;
}

void *flash_ack2clnt(void *param){
	void **params=(void**)param;
	uint8_t type=*((uint8_t*)params[0]);
	//uint32_t *tt;
	//uint32_t seq=*((uint32_t*)params[1]);
	switch(type){
		case FS_NOTFOUND_T:
		case FS_GET_T:
			/*
			   kuk_ack2clnt(net_worker);*/
			//kuk_send(net_worker,(char*)&seq,sizeof(seq));
			if(*(uint32_t*)params[1]!=0){
				while(!q_enqueue((void*)params[1],ret_q)){}
			}
			break;
			/*
			   case FS_SET_T:
			   printf("return req:%u\n",*(uint32_t*)params[1]);
			   if(*(uint32_t*)params[1]!=0){
			   while(!q_enqueue((void*)params[1],ret_q)){}
			   }
			   break;*/
		default:
			break;
	}

	free(params[0]);
	//free(params[1]);
	free(params);
	return NULL;
}


int main(void){
	int server_socket;
	socklen_t client_addr_size;

	struct sigaction sa;
	sa.sa_handler = log_print;
	sigaction(SIGINT, &sa, NULL);

	q_init(&ret_q,8192*128);

	inf_init();
	pthread_t t_id;
	pthread_create(&t_id,NULL,flash_returner,NULL);

	int option;


	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;

	server_socket = socket( PF_INET, SOCK_STREAM, 0);
	option = 1;
	setsockopt( server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option) );

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family     = af_inet;
	server_addr.sin_port       = htons(port);
	server_addr.sin_addr.s_addr= inet_addr(ip);

	if(-1 == bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr))){
		printf("bind error!\n");
		exit(1);
	}

	if(-1 == listen(server_socket, 5)){
		exit(1);
	}
	bench_init(1);
	bench_add(NOR,0,-1,-1);
	char t_value[PAGESIZE];
	memset(t_value,'x',PAGESIZE);
	value_set dummy;
	dummy.value=t_value;
	dummy.dmatag=-1;
	dummy.length=PAGESIZE;

	printf("server waiting.....\n");
	client_addr_size  = sizeof( client_addr);
	client_socket     = accept( server_socket, (struct sockaddr*)&client_addr, &client_addr_size);
	printf("connected!!!\n");
	if (-1 == client_socket){
		exit(1);
	}


	int flag;
	flag = fcntl( client_socket, F_GETFL, 0 );
	fcntl( client_socket, F_SETFL, flag | O_NONBLOCK );

	net_data_t temp[1024];
	int readed=0,len;
	while(1){
		memset(temp,0,sizeof(temp));
		readed=len=0;
		while(readed==0 || (readed%sizeof(net_data_t))){
			len=read (client_socket,&((char*)temp)[readed],sizeof(temp)-readed);
			if(len>0){
				readed+=len;
			}
		}
		//	printf("readed len :%d %d sizseoftemp:%d\n",readed, len,sizeof(temp));
		//printf("readed %u %u\n",readed/sizeof(net_data_t),readed%sizeof(net_data_t));
		for(uint32_t i=0;i<readed/sizeof(net_data_t); i++){
			net_data_t *t=&temp[i];
			//			if(t->type==1){
			//			printf("netdata: %d %llu %llu(len) %u\n",t->type,t->offset,t->len,t->seq);
			//fprintf(stderr, "%d %llu %llu\n",t->type,t->offset,t->len);
			//			}
			if(t->len>256){
				printf("len %d readed:%d\n",len,readed);
				printf("DATA:%d %lu %lu %u\n",t->type,t->offset,t->len,t->seq);
				printf("aaaaaaaa\n");
			}
			for(uint32_t j=0; j<t->len; j++){
				if(j+1!=t->len){
					inf_make_req_special(t->type,(uint32_t)t->offset+j,&dummy,0,flash_ack2clnt);
				}else{
					inf_make_req_special(t->type,(uint32_t)t->offset+j,&dummy,t->seq,flash_ack2clnt);
#ifdef WRITESYNC
					if(t->type==1){
						//					printf("why\n");
						write_return=(uint32_t*)malloc(sizeof(uint32_t));
						*write_return=t->seq;
						while(!q_enqueue((void*)write_return,ret_q)){}
					}
#endif
				}
			}
		}
	}
}
