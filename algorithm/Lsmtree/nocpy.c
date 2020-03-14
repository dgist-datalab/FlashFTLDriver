#include "nocpy.h"
#include "../../include/lsm_settings.h"
#include "../../include/settings.h"
#include "../../interface/queue.h"
#include "../../blockmanager/bb_checker.h"
extern lsmtree LSM;
keyset **page;
extern bb_checker checker;
queue *delayed_trim_queue;
void nocpy_init(){
	page=(keyset**)malloc(sizeof(keyset*)*((MAPPART_SEGS)*_PPS));
	for(int i=0; i<(MAPPART_SEGS)*_PPS; i++){
		//page[i]=(keyset*)malloc(PAGESIZE);
		page[i]=NULL;
	}
	printf("------------# of copy%ld\n",(MAPPART_SEGS)*_PPS);
	q_init(&delayed_trim_queue,((MAPPART_SEGS)*_PPS+1));
}

void nocpy_free_page(uint32_t ppa){
	free(page[ppa]);
	page[ppa]=NULL;
}

void nocpy_free_block(uint32_t ppa){
	printf("trim:%d\n",ppa);
	for(uint32_t i=ppa; i<ppa+_PPS; i++){
		if(!page[i]) continue;
		free(page[i]);
		page[i]=NULL;
	}
}

void nocpy_copy_to(char *des, uint32_t ppa){
	if(page[ppa]==NULL) page[ppa]=(keyset*)malloc(PAGESIZE);
	memcpy(page[ppa],des,PAGESIZE);
}


void nocpy_free(){
	for(int i=0; i<(MAPPART_SEGS)*_PPS; i++){
//		fprintf(stderr,"%d %p\n",i,page[i]);
		free(page[i]);
	}
	free(page);
	nocpy_trim_delay_flush();
}

void nocpy_copy_from_change(char *des, uint32_t ppa){
	if(page[ppa]){
		printf("existence error %d\n",ppa);
		abort();
		free(page[ppa]);
		page[ppa]=NULL;
	}

#if (LEVELN!=1) && !defined(KVSSD)
	if(((keyset*)des)->lpa>1024 || ((keyset*)des)->lpa<=0){
		abort();
	}
#endif
	page[ppa]=(keyset*)des;
}

char *nocpy_pick(uint32_t ppa){
#if (LEVELN!=1) && !defined(KVSSD)
	if(page[ppa]->lpa<=0 || page[ppa]->lpa>1024) abort();
#endif
	return (char*)page[ppa];
}
void nocpy_force_freepage(uint32_t ppa){
	page[ppa]=NULL;
}

uint32_t nocpy_size(){
	uint32_t res=0;
	for(int i=0; i<(MAPPART_SEGS)*_PPS; i++){
		if(page[i]) res+=PAGESIZE;
	}
	return res;
}

void nocpy_trim_delay_enq(uint32_t ppa){
	//static int cnt=1;
	//for(uint32_t i=ppa; i<ppa+_PPB; i++){
		if(!q_enqueue((void*)page[ppa],delayed_trim_queue)){
			printf("error in nocpy_enqueue!\n");
			abort();
		}	
		page[ppa]=NULL;
	//}
}

void nocpy_trim_delay_flush(){
	int target_size=delayed_trim_queue->size;
	for(int i=0; i<target_size; i++){
		void *req=q_dequeue(delayed_trim_queue);
		if(req==NULL) continue;
		free(req);
	}
}
