#ifndef __HEAP_HEADER
#define __HEAP_HEADER
#include "../settings.h"
#define MHPARENTPTR(mh,idx) &((mh->body)[idx/2])
#define MHL_CHIPTR(mh,idx) &((mh->body)[idx*2])
#define MHR_CHIPTR(mh,idx) &((mh->body)[idx*2+1])

typedef struct heap_node{
	double cnt;
	void *data;
}hn;

typedef struct max_heap{
	hn* body;
	int size;
	int max;
	void (*swap_hptr)(void * a, void *b);
	void (*assign_hptr)(void *a, void* mh);
	double (*get_cnt)(void *a, void *b);
	void *bm;
}mh;

void mh_init(mh**, int bn, void (*swap_hptr)(void*,void*), void(*aassign_hptr)(void *, void *),double (*get_cnt)(void *a, void *b), void *bm);
void mh_free(mh*);
void mh_insert(mh*, void *data, int number);
void mh_insert_append(mh *, void *data);
void mh_construct(mh *);
void* mh_get_max(mh*);
void mh_update(mh*,int number, void *hptr);
void mh_print(mh *, void (*print_func)(void *blk));
#endif
