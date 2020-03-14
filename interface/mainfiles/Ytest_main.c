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
#include <getopt.h>

#include "../../include/settings.h"
#include "../../bench/bench.h"
#include "../interface.h"
#include "../queue.h"
#include "../../include/flash_sock/fd_sock.h"


MeasureTime temp;
int client_socket;
queue *n_q;

void log_print(int sig){
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

void kv_main_end_req(uint32_t a, uint32_t b, void *req){
	if(req==NULL) return;
	netdata *net_data=(netdata*)req;
//	net_data->seq=a;
	switch(net_data->type){
		case FS_GET_T:
			//printf("insert_queue\n");
	//		while(!q_enqueue((void*)net_data,n_q));
	//		printf("assign seq:%d\n",a);
			free(net_data);
			break;
		case FS_RANGEGET_T:
		case FS_SET_T:
			free(net_data);
			break;
	}
}

MeasureTime write_opt_time[10];
int trace_set_type(int argc, char *argv[], char **targv, char **files){
	struct option options[]={
		{"file",1,0,0},
		{0,0,0,0}
	};
	int temp_cnt=0;
	for(int i=0; i<argc; i++){
		if(strncmp(argv[i],"--file",strlen("--file"))==0){
			i++;
			continue;
		}
		targv[temp_cnt++]=argv[i];
	}

	int opt;
	bool file_setting=false;
	int index;
	int file_cnt=0;
	opterr=0;
	while((opt=getopt_long(argc,argv,"",options,&index))!=-1){
		switch(opt){
			case 0:
				if(optarg!=NULL){
					strcpy(files[file_cnt++],optarg);
					file_setting=true;
				}
		}
	}
	
	if(!file_setting){
		printf("plz input filename!\n");
	}
	printf("%d file input ready!\n",file_cnt);
	for(int i=0; i<file_cnt; i++){
		printf("--%d %s\n",i,files[i]);
	}
	optind=0;
	return temp_cnt;
}

extern master_processor mp;
int main(int argc, char *argv[]){
	/*struct sigaction sa;
	sa.sa_handler = log_print;
	sigaction(SIGINT, &sa, NULL);
	printf("signal add!\n");*/
	char *temp_argv[20];
	char **filearr=(char**)malloc(sizeof(char*)*2);
	for(int i=0; i<2; i++) filearr[i]=(char*)calloc(256,1);
	int temp_cnt=trace_set_type(argc,argv,temp_argv,filearr);
	inf_init(1,0,temp_cnt,temp_argv);
	netdata *data;
	char temp[8192]={0,};
	data=(netdata*)malloc(sizeof(netdata));
	static int cnt=0;
	static volatile int req_cnt=0;
	//measure_init(&data->temp);
	
	bench_custom_init(write_opt_time,10);
	for(int i=0; i<1; i++){
		FILE *fp = fopen(filearr[i], "r");
		bench_custom_start(write_opt_time,i);
		while(fscanf(fp,"%d %d %d %d %s %d\n",&data->type,&data->keylen,&data->seq,&data->scanlength,data->key,&data->valuelen)!=EOF){
			if(data->type==1|| data->type==2){
				inf_make_req_apps(data->type,data->key,data->keylen,temp,512,cnt++,NULL,kv_main_end_req);	
			}
			else{
				data->type=FS_RANGEGET_T;
				inf_make_range_query_apps(data->type,data->key,data->keylen,cnt++,data->scanlength,NULL,kv_main_end_req);
			}
			if(req_cnt++%10240==0){
				printf("\r%d gc_test",req_cnt);
				fflush(stdout);
			}
			if(req_cnt%10000000==0){
				printf("\nlog %d req_cnt\n",req_cnt);
				for(int i=0; i<LREQ_TYPE_NUM;i++){
					fprintf(stderr,"%s %lu\n",bench_lower_type(i),mp.li->req_type_cnt[i]);
				}
			}
	//		data=(netdata*)malloc(sizeof(netdata));
		}
		bench_custom_A(write_opt_time,i);
	}
	bench_custom_print(write_opt_time,10);
}
