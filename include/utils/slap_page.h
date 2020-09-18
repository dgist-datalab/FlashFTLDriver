#ifndef __SLAP_P_H__
#define __SLAP_P_H__
#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <sys/mman.h>
#include "tag_q.h"

#define HP_PATH "/dev/hugepages/kukania_memory"

enum{
	SP_WRITE, SP_READ, SP_WHATEVER
};

typedef struct slap_page_manager{
	tag_manager *tag_m;
	char **storage;
	uint64_t allocated_size;
	uint32_t nop;
}spm;

void spm_init(uint32_t numof_physical_page);
void spm_free();
int spm_memory_alloc(int type, char **buf);
void spm_memory_free(int type, int tag);
#endif
