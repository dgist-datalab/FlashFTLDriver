#include "PM_blockqueue.h"

void Init_Bqueue(B_queue *queue)//initialize queue for binfo.
{
	queue->front = queue->rear = NULL; //single linked, no comp. so NULL
	queue->count = 0; //no comp now
}

int IsEmpty(B_queue *queue)
{
	return queue->count == 0; 
}

void Enqueue(B_queue *queue, int _idx)
{
	node_bqueue *now = (node_bqueue *)malloc(sizeof(node_bqueue));
	now->idx = _idx;
	now->next = NULL; //make node_bqueue and allocate information.
	
	if (IsEmpty(queue)) // if queue is empty
		queue->front = now;
	else //if queue is not empty
	{
		queue->rear->next = now; //set CURRENT  rear comp's next as now.
	}
	queue->rear = now; //change rear pointaer to  now.
	queue->count++; //component number inc.
}

int Dequeue(B_queue *queue)
{
	int re = -1;
	node_bqueue* now;
	if (IsEmpty(queue))
	{
		return re;
	}
	now = queue->front;
	re = now->idx;
	queue->front = now->next;
	free(now);
	queue->count--;
	return re;
}


