#include "page.h"

B_queue free_queue;
B_queue reserved_queue;
int local_count = 0; //idx of local page in blk.

extern int64_t PPA_status;

void PM_init(){
	/*this function initializes blockinfo queue.
	  allocate 1 block to reserved queue.
	  allocate others to free queue.
	*/
	int op = 1; //num of overprovision block.
	Init_Bqueue(&free_queue);
	Init_Bqueue(&reserved_queue);
	
	for(int i=0;i<_NOB;i++){
		if(op != 0){
			Enqueue(&reserved_queue,i);
			op--;
		}
		else
			Enqueue(&free_queue,i);
	}
}

void PM_destroy(){
	/*this function frees binfo blocks.
	  finds current free & reserved binfo node.
	  frees those nodes.
	*/
	int current_free = free_queue.count;
	int current_reserved = reserved_queue.count;
	
	for (int i=0;i<current_free;i++)
		Dequeue(&free_queue);
	for (int i=0;i<current_reserved;i++)
		Dequeue(&reserved_queue);
}


int64_t alloc_page(){
	/*this function gives free page.
	  uses block idx and local count.
	*/
	int ret = -1; //return value.
	if((PPA_status == -1) ||
		local_count)
		ret = Dequeue(&free_queue);

	else
		ret = PPA_status + 1;

}


