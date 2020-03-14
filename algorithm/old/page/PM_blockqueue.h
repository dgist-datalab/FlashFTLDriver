#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/settings.h"

#define OP_area 1

//this would be the queue structure for Page_selector
//use linked list to build queue.

typedef struct block_info {
	uint64_t head_ppa;
	unsigned char BAD;
}BINFO;

//stores info about each block.
//!!MUST BE MATCHED WITH BADBLOCK_MANAGER FORMAT.!!

typedef struct node {
	BINFO* BINFO_node;
	struct node *next;
}node;
//each node has pointer for BINFO and another pointer to next node.

typedef struct Block_queue
{
	node *front;
	node *rear;
	int count;
}B_queue;
//structure for queue managing.

void Init_Bqueue(B_queue *queue);
int IsEmpty(B_queue *queue);
void Enqueue(B_queue *queue, BINFO* new_node);
BINFO* Dequeue(B_queue *queue);

