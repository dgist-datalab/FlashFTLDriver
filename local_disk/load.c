#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#define LEN 256*2
int main(int argc, char * argv[]){
    float ary[LEN] ;
    int fd;
    int size;

    if((fd = open(argv[1], O_RDONLY)) == -1){
	printf("erro\n");
	return -1;
    }

    if( (size = pread(fd, ary, LEN*sizeof(float), 0)) == -1){
	printf("error\n");
	return -1;
    }
    close(fd);
    for(int i=0; i<LEN; ++i){
	printf("%d %f\n",i+1, ary[i]);
    }

	return 0;

}
