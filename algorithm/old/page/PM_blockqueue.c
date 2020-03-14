#include "PM_BlockQueue.h"

void Init_Bqueue(B_queue *queue)//initialize queue for binfo.
{
	queue->front = queue->rear = NULL; //single linked, no comp. so NULL
	queue->count = 0; //no comp now
}

int IsEmpty(B_queue *queue)
{
	return queue->count == 0; 
}

void Enqueue(B_queue *queue, BINFO* new_info)
{
	node *now = (node *)malloc(sizeof(node));
	now->BINFO_node = new_info;
	now->next = NULL; //make node and allocate information.
	
	if (IsEmpty(queue)) // if queue is empty
		queue->front = now;
	else //if queue is not empty
	{
		queue->rear->next = now; //set CURRENT  rear comp's next as now.
	}
	queue->rear = now; //change rear pointaer to  now.
	queue->count++; //component number inc.
}

BINFO* Dequeue(B_queue *queue)
{
	BINFO* re = NULL;
	node* now;
	if (IsEmpty(queue))
	{
		return re;
	}
	now = queue->front;
	re = now->BINFO_node;
	queue->front = now->next;
	free(now);
	queue->count--;
	printf("re? %d, %d\n", re->BAD, re->head_ppa);
	return re;
	
}


