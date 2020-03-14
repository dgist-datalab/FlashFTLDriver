#include "select_operation.h"

int64_t local_page_position = 0;
int64_t block_position = -1;
int64_t reserved_local = 0;
int64_t reserved_block = -1;
//use this global parameters to count 

void Selector_Init(BINFO** bp)//initializes selector.
{
	// 0.set block #0 as overprovision area.
	// 1.get Block_information
	// 2.attach on empty_queue.
	int OP_builder = 0;
	Init_Bqueue(&reserved_queue);
	Init_Bqueue(&empty_queue);
	for (int i=0; i < NOB; i++)
	{
		if ((OP_builder < OP_area) && (bp[i]->BAD == 0))
		{
			Enqueue(&reserved_queue, bp[i]);
			OP_builder++;
		}//build overprovision area.
		else if ((OP_builder >= OP_area) && (bp[i]->BAD == 0))
		{
			Enqueue(&empty_queue, bp[i]);
		}//make empty_queue.
	}
}

void Selector_Destroy()
{
	int current_empty = empty_queue.count;
	int current_reserved = reserved_queue.count;//get count of current queue.
	for (int i = 0; i < current_empty; i++)
		Dequeue(&empty_queue);
	for (int i = 0; i < current_reserved; i++)
		Dequeue(&reserved_queue);
//dequeue until all components die.
}

uint64_t Giveme_Page(int reserved)
{
	if (reserved == 0)
	{
		
		if ((block_position == -1) || (local_page_position == PPB))
		{
			BINFO* newbie = Dequeue(&empty_queue);
			block_position = newbie->head_ppa / PPB;
			local_page_position = 1;
			return newbie->head_ppa;
		}//if it is first write or block is full, get another block.
		else
		{
			uint64_t re = block_position * PPB + local_page_position;
			local_page_position++;
			return re;
		}//if not, increase local_position && give page num.
	}
	else if (reserved == 1)
	{
		if (reserved_block = -1 || reserved_local == PPB)
		{
			BINFO* newbie = Dequeue(&reserved_queue);
			reserved_block= newbie->head_ppa / PPB;
			return newbie->head_ppa;
		}//if it is first time using reserved, get block.
		else
		{
			uint64_t re = reserved_block * PPB + reserved_local;
			reserved_local++;
			return re;
		}//if not, increase local_position && give page num.
	}
}

uint64_t Set_Free(BINFO**bp, int block_number, int reserved)
{
	if (reserved == 0)
		Enqueue(&empty_queue, bp[block_number]);
	else if (reserved == 1)
		Enqueue(&reserved_queue, bp[block_number]);
}//after GC, Enqueue blocks with this function.
