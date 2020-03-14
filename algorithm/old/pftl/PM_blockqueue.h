#ifndef _PM_BLOCKQUEUE_H_
#define _PM_BLOCKQUEUE_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/settings.h"
#include "../../BM_Interface.h"

//this would be the queue structure for Page_selector
//use linked list to build queue.

typedef struct node_bqueue {
	int idx;
	struct node_bqueue *next;
}node_bqueue;
//each node_bqueue has pointer for Block and another pointer to next node.

typedef struct Block_queue
{
	node_bqueue *front;
	node_bqueue *rear;
	int count;
}B_queue;
//structure for queue managing.

void Init_Bqueue(B_queue *queue);
int IsEmpty(B_queue *queue);
void Enqueue(B_queue *queue, int _idx);
int Dequeue(B_queue *queue);

#endif // !_PM_BLOCKQUEUE_H_
