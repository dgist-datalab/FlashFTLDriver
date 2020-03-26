#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>


#define PORT 9001
#define LEN 256
#define BATCH_NUM 4
#define ENTRY_NUM (BATCH_NUM *2)
#define TABLE_NUM 2

char            data[7] ="Hello!";
float		data2[2] = {1.2223, 2.33221};
int             serversock, clientsock;
void            quit(char* msg,int retval);
// 0번 테이블의 1번 entry (256개)
// 0번 테이블의 2번 entry (256개)

// page size / entry size = 4K / 1K = 4
// 0번 테이블에서 n/4 * page_size ~ +page_size(4096) ----- n%4 ~ + entry_size(1024)

struct recv_data_t{
	int num[2];
	int offset[2];
};

struct send_data_t{
	int  table_num;
	int  offset;
	float value[LEN];
};

int main(int argc,char** argv)
{
	struct sockaddr_in      server, client;
	int                     accp_sock;
	int                     addrlen =sizeof(client);
	int                     bytes;
	int                     dataSize =sizeof(data2);
	int                     **from_client;
	int                     **req;
	int                     i;
	struct recv_data_t	*r_data;
	struct send_data_t	*s_data;

/*
	from_client = (int**)malloc(sizeof(int*)*TABLE_NUM);
	for(int i=0; i<TABLE_NUM; ++i)
		from_client[i] = (int*)malloc(sizeof(int)*ENTRY_NUM); 

	req = (int**)malloc(sizeof(int*)*BATCH_NUM);
	for(int i=0; i<TABLE_NUM; ++i)
		req[i] = (int*)malloc(sizeof(int)*ENTRY_NUM); 


	printf("Data Size is : %d\n", dataSize);
*/

	r_data = (struct recv_data_t*)malloc(sizeof(struct recv_data_t));
	s_data = (struct send_data_t*)malloc(sizeof(struct send_data_t));
	//s_data->value = (float*)malloc(sizeof(float)*LEN);


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

/*
	bytes = send(accp_sock, &data2, dataSize, 0);

	if(bytes != dataSize){
		fprintf(stderr,"Connection closed. bytes->[%d], dataSize->[%d]\n",bytes, dataSize);
		close(accp_sock);

		if ((accp_sock = accept(serversock, NULL, NULL)) == -1) {
			quit("accept() failed", 1);
		}
	}
*/

	// file open
	int fd;
	int size;

/*
	if((fd = open(argv[1], O_RDONLY)) == -1){
		printf("erro\n");
		return -1;
	}

	if( (size = pread(fd, ary, LEN*sizeof(float), 0)) == -1){
		printf("error\n");
		return -1;
	}
	close(fd);
*/

//

	// Data Receive
	// bytes re-init
//emb_l.1.weight.txt
	printf("Get Data from client\n");

	char base[] = "emb_l.";
	char last[] = ".weight.txt";
	char numb[2];
	char dir[100];
	while(1){
		// receive data
		bytes = 0;
		for(i=0; i<sizeof(struct recv_data_t); i+= bytes){
			if ((bytes = recv(accp_sock, r_data + i,sizeof(struct recv_data_t) - i, 0)) == -1) {
				quit("recv failed", 1);
			}
		}

		// load data
		printf("%d %d\n", r_data->num[0], r_data->offset[0]);
		memset(dir, 0, sizeof(dir));
		strcpy(dir, "save_file/emb_l.");
		//strcat(dir, base);
		sprintf(numb, "%d", r_data->num[0]);
		strcat(dir, numb);
		strcat(dir, last);

		if( (fd = open(dir, O_RDONLY)) == -1){
			printf("open erro\n");
			printf("%s\n",dir);
			return -1;
		}
		s_data->table_num = r_data->num[0];
		s_data->offset = r_data->offset[0];

		if( (size = pread(fd, &(s_data->value), sizeof(float)*LEN, r_data->offset[0]*sizeof(float)*LEN )) == -1){
			printf("read error\n");
			return -1;
		}
		close(fd);

			
		// send data
		bytes = send(accp_sock, s_data, sizeof(struct send_data_t), 0);

		if(bytes != sizeof(struct send_data_t)){
			fprintf(stderr,"Connection closed. bytes->[%d], dataSize->[%d]\n",bytes, sizeof(struct send_data_t));
			close(accp_sock);

			if ((accp_sock = accept(serversock, NULL, NULL)) == -1) {
				quit("accept() failed", 1);
			}
		}	
		//break;
	}
	/*
	for(int j=0; j< TABLE_NUM; ++j){
		bytes=0;
		for(i=0; i<sizeof(int)*ENTRY_NUM; i+= bytes){
			if ((bytes = recv(accp_sock, &from_client[j][0] + i,sizeof(int)*ENTRY_NUM - i, 0)) == -1) {
				quit("recv failed", 1);
			}
		}
	}
	printf("from_client size : %d\n", sizeof(from_client));
	for(int j=0; j<TABLE_NUM; ++j){
		for(int i=0; i<ENTRY_NUM; i+=2)
			printf("%d\n", from_client[j][i]);
	}
	//printf(" from_client size : %d, Contents : %d\n",sizeof(from_client), from_client[4]);
*/

	quit(NULL, 0);
}

void quit(char* msg,int retval)
{
	if (retval == 0) {
		fprintf(stdout, (msg == NULL ?"" : msg));
		fprintf(stdout,"\n");
	}else {
		fprintf(stderr, (msg == NULL ?"" : msg));
		fprintf(stderr,"\n");
	}

	if (clientsock) close(clientsock);
	if (serversock) close(serversock);

	exit(retval);
}

