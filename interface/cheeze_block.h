#ifndef __CHEEZE_INF_H__
#define __CHEEZE_INF_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../include/container.h"

#define OP_READ 0
#define OP_WRITE 1

struct cheeze_req {
	int rw;
	volatile int acked;
	unsigned int index;
	unsigned int offset;
	unsigned int size;
	unsigned long id;
	void *addr;
	void *user_buf;
} __attribute__((aligned(8), packed));

typedef struct cheeze_req cheeze_req;

#define COPY_TARGET "/tmp/vdb"

void init_cheeze();
void free_cheeze();
vec_request *get_vectored_request();

#endif
