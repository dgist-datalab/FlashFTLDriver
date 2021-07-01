#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <getopt.h>
#include "../../include/data_struct/lrucache.hpp"
#include "page.h"
#include "map.h"

#include "../../bench/bench.h"
extern uint32_t test_key;
align_buffer a_buffer;
typedef std::multimap<uint32_t, algo_req*>::iterator rb_r_iter;

extern MeasureTime mt;
#if 1 //NAM
int debugNAM = 0; 
extern uint64_t request_num; //request number 
dawid_buffer d_buffer;
node_dirty* dHead = NULL; 
ht h; 
extern unsigned char dirty_option; 
extern unsigned char node_dirty_option; 
extern unsigned char heap_option; 
#endif
struct algorithm page_ftl={
	.argument_set=page_argument,
	.create=page_create,
	.destroy=page_destroy,
	.read=page_read,
	.write=page_write,
	.flush=page_flush,
	.remove=page_remove,
};

page_read_buffer rb;

uint32_t page_create (lower_info* li,blockmanager *bm,algorithm *algo){
	algo->li=li; //lower_info means the NAND CHIP
	algo->bm=bm; //blockmanager is managing invalidation 

#if 1 //NAM
	printf("total_size: %ld\n", TOTALSIZE);
	printf("_DCE: %d\n", _DCE);
	d_buffer.htable = (bucket*)malloc(_DCE*sizeof(bucket)); 
	for(uint32_t i = 0; i < _DCE; i++){ 
		d_buffer.htable[i].idx = i; 
		d_buffer.htable[i].count = 0; 
	} 
	h.heap = (element*)malloc(_DCE*sizeof(element)); 
	h.heap_size = 0; 
#endif
	page_map_create();

	rb.pending_req=new std::multimap<uint32_t, algo_req *>();
	rb.issue_req=new std::multimap<uint32_t, algo_req*>();
	fdriver_mutex_init(&rb.pending_lock);
	fdriver_mutex_init(&rb.read_buffer_lock);
	rb.buffer_ppa=UINT32_MAX;
	return 1;
}

void page_destroy (lower_info* li, algorithm *algo){
	//page_map_free();
	delete rb.pending_req;
	delete rb.issue_req;
	return;
}

#if 1  
inline void send_user_req(request *const req, uint32_t type, ppa_t ppa,value_set *value){
	/*you can implement your own structur for your specific FTL*/
#if 1
	if(type==DATAR){
		fdriver_lock(&rb.read_buffer_lock);
		if(ppa==rb.buffer_ppa){
			if(test_key==req->key){
				printf("%u page hit(piece_ppa:%u)\n", req->key,value->ppa);
			}
			memcpy(value->value, &rb.buffer_value[(value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			req->type_ftl=req->type_lower=0;
			req->end_req(req);
			fdriver_unlock(&rb.read_buffer_lock);
			return;
		}
		fdriver_unlock(&rb.read_buffer_lock);
	}
#endif
	page_param* param=(page_param*)malloc(sizeof(page_param));
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req));
	param->value=value;
	my_req->parents=req;//add the upper request
	my_req->end_req=page_end_req;//this is callback function
	my_req->param=(void*)param;//add your parameter structure 
	my_req->type=type;//DATAR means DATA reads, this affect traffics results
	/*you note that after read a PPA, the callback function called*/

#if 1
	if(type==DATAR){
		fdriver_lock(&rb.pending_lock);
		rb_r_iter temp_r_iter=rb.issue_req->find(ppa);
		if(temp_r_iter==rb.issue_req->end()){
			rb.issue_req->insert(std::pair<uint32_t,algo_req*>(ppa, my_req));
			fdriver_unlock(&rb.pending_lock);
		}
		else{
			rb.pending_req->insert(std::pair<uint32_t, algo_req*>(ppa, my_req));
			fdriver_unlock(&rb.pending_lock);
			return;
		}
	}
#endif
	switch(type){
		case DATAR:
			page_ftl.li->read(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
		case DATAW:
			page_ftl.li->write(ppa,PAGESIZE,value,ASYNC,my_req);
			break;
	}
}
#endif

#if 0 //NAM
inline void send_user_req(request *const req, uint32_t type, ppa_t ppa,value_set *value){ 
	page_param* params=(page_param*)malloc(sizeof(page_param)); 
	algo_req *my_req=(algo_req*)malloc(sizeof(algo_req)); 
	params->value=value; 
	my_req->parents=req; 
	my_req->end_req=page_end_req; 
	my_req->param=(void*)params; 
	my_req->type=type; 

	switch(type){ 
		case DATAR: 
			page_ftl.li->read(ppa,PAGESIZE,value,ASYNC,my_req); 
			break;
		case DATAW: 
			page_ftl.li->write(ppa,PAGESIZE,value,ASYNC,my_req); 
			break; 
	} 
} 
#endif

bool testing;
uint32_t testing_lba;

uint32_t page_read(request *const req){

	if(!testing){
		testing_lba=req->key;
		testing=true;
	}

//	printf("issue %u %u\n", req->seq, req->key);

	for(uint32_t i=0; i<a_buffer.idx; i++){
		if(req->key==a_buffer.key[i]){
			//		printf("buffered read!\n");
			memcpy(req->value->value, a_buffer.value[i]->value, LPAGESIZE);
			req->end_req(req);		
			return 1;
		}
	}

	//printf("read key :%u\n",req->key);

	req->value->ppa=page_map_pick(req->key);
	if(req->key==test_key){
		printf("read: map info - %u->%u\n", req->key, req->value->ppa);
	}

	//DPRINTF("\t\tmap info : %u->%u\n", req->key, req->value->ppa);
	if(req->value->ppa==UINT32_MAX){
		req->type=FS_NOTFOUND_T;
		req->end_req(req);
	}
	else{
		send_user_req(req, DATAR, req->value->ppa/L2PGAP, req->value);
	}
	return 1;
}
#if 1 //NAM
pNode* get_node(request *req, KEYT key, value_set *value){ 
	pNode* newNode; 
	newNode = (struct pNode*)malloc(sizeof(struct pNode)); 

	newNode->req = req; 
	newNode->key = req->key; 
	newNode->value = req->value; 
	newNode->next = NULL; 

	return newNode; 
} 
#endif

#if 1 //NAM
uint32_t find_idx(KEYT key){ 
	/* find index on map table page associated with key(LPN) */ 
	return key >> _PMES; 
} 
#endif

uint32_t align_buffering(request *const req, KEYT key, value_set *value){
	if(req){
		a_buffer.value[a_buffer.idx]=req->value;
		a_buffer.key[a_buffer.idx]=req->key;
	}
	else{
		a_buffer.value[a_buffer.idx]=value;
		a_buffer.key[a_buffer.idx]=key;
	}
	a_buffer.idx++;

	if(a_buffer.idx==L2PGAP){
		ppa_t ppa=page_map_assign(a_buffer.key, a_buffer.idx);
		value_set *value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
		for(uint32_t i=0; i<L2PGAP; i++){
			memcpy(&value->value[i*LPAGESIZE], a_buffer.value[i]->value, LPAGESIZE);
			inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
		}
		send_user_req(NULL, DATAW, ppa, value);
		a_buffer.idx=0;
	}
	return 1;
}

#if 1 //NAM
int32_t dawid_buffering(request *const req, KEYT key, value_set *value){ 
	pNode* newNode; 
	uint32_t idx = find_idx(req->key);
#if 1
	if(idx < 0 || idx >= _DCE){ 
		return -1;
	} 
#endif

	if(is_page_dirty(idx)){ 
		pageIsDirty(idx); 
	}  
	else{
		pageIsClean(idx); 
	}

	if(req)
		newNode = get_node(req, req->key, req->value); 
	else
		newNode = get_node(NULL, key, value);

	if(!newNode){ 
		return -1;
	}  
	
	if(d_buffer.htable[idx].count > 0)
		newNode->next = d_buffer.htable[idx].head; 

	d_buffer.htable[idx].head = newNode; 
	d_buffer.htable[idx].count++; 
	d_buffer.tot_count++; 

	return 1; 
}
#endif

#if 1 //NAM
int32_t dequeue_and_flush(){ 
	pNode* node; 
	node_dirty* dNode; 
	int cmax = -1, imax = -1; 
	
	pm_body *p=(pm_body*)page_ftl.algo_body; 
#if 1 
	while(dHead != NULL){ 
		dNode = dHead; 
		cmax = d_buffer.htable[dNode->idx].count; 
		imax = dNode->idx; 
		
		d_buffer.htable[imax].count -= cmax; 
		d_buffer.tot_count -= cmax; 
		
		while(d_buffer.htable[imax].head != NULL){ 
			node = d_buffer.htable[imax].head; 
			align_buffering(node->req, 0, NULL); 
			d_buffer.htable[imax].head = node->next; 
		
			node->req->value = NULL; 
			node->req->end_req(node->req); 
			free(node); 
		} 
		//p->dirty_check[imax] &= ~node_dirty_option;
		dHead = dNode->next; 
		free(dNode); 
	}
#endif	
	if(h.heap_size >= 1){ 
		cmax = h.heap[1].count; 
		imax = h.heap[1].idx; 
		
		if(d_buffer.htable[imax].count == 0){ 
			delete_max_heap(); 
			cmax = h.heap[1].count; 
			imax = h.heap[1].idx; 
		}
		d_buffer.htable[imax].count = 0; 
		d_buffer.tot_count -= cmax; 
	
		while(d_buffer.htable[imax].head != NULL){
#if 1 
			node = d_buffer.htable[imax].head; 
			align_buffering(node->req, 0, NULL); 
			d_buffer.htable[imax].head = node->next; 
			node->req->value = NULL; 
			node->req->end_req(node->req); 
			free(node);
#endif 
		} 
		delete_max_heap();
		return imax; 
	}

	return -1; 
}
#endif 

#if 0
uint32_t page_write(request *const req){
	//printf("write key :%u\n",req->key);
	align_buffering(req, 0, NULL);
	req->value=NULL;
	req->end_req(req);
	//send_user_req(req, DATAW, page_map_assign(req->key), req->value);
	return 0;
}
#endif
#if 1 //NAM
uint32_t page_write(request *const req){ 
	int req_seq = req->seq;

#if 1
	dawid_buffering(req, 0, NULL);
	if(d_buffer.tot_count == WBUFF_SIZE){
	//if(d_buffer.tot_count == QSIZE){
		int32_t fidx = dequeue_and_flush(); 
		if(fidx < 0){ 
			return -1; 
		} 
	}
#endif
#if 1 
//	if(req_seq == ((request_num/2)-1)){ 	
	if(req_seq == (request_num - 1)){ 
#if 0
		if(dHead != NULL)
			printf("dHead is not NULL !\n"); 
#endif
		for(uint32_t i = 0; i < _DCE; i++){ 
			uint32_t reqs = d_buffer.htable[i].count; 

			if(reqs > 0){ 
				d_buffer.tot_count -= reqs; 
				
				struct pNode* node; 
				while(d_buffer.htable[i].head != NULL){ 
					node = d_buffer.htable[i].head; 
					align_buffering(node->req, 0, NULL); 
					d_buffer.htable[i].head = node->next;

					node->req->value = NULL; 
					node->req->end_req(node->req); 
					free(node); 
				}	
		
			}
		} 
	}
#endif
	return 0; 
} 
#endif

uint32_t page_remove(request *const req){

	for(uint8_t i=0; i<a_buffer.idx; i++){
		if(a_buffer.key[i]==req->key){
			inf_free_valueset(a_buffer.value[i], FS_MALLOC_W);
			if(i==1){
				a_buffer.value[0]=a_buffer.value[1];
				a_buffer.key[0]=a_buffer.key[1];
			}

			a_buffer.idx--;
			goto end;
		}
	}
	
	page_map_trim(req->key);
end:
	req->end_req(req);
	return 0;
}

uint32_t page_flush(request *const req){
	abort();
	req->end_req(req);
	return 0;
}
static void processing_pending_req(algo_req *req, value_set *v){
	request *parents=req->parents;
	page_param *param=(page_param*)req->param;
	memcpy(param->value->value, &v->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
	parents->type_ftl=parents->type_lower=0;
	parents->end_req(parents);
	free(param);
	free(req);
}

void *page_end_req(algo_req* input){
	//this function is called when the device layer(lower_info) finish the request.
	rb_r_iter target_r_iter;
	rb_r_iter target_r_iter_temp;
	algo_req *pending_req;
	page_param* param=(page_param*)input->param;
	switch(input->type){
		case DATAW:
			inf_free_valueset(param->value,FS_MALLOC_W);
			break;
		case DATAR:
			fdriver_lock(&rb.pending_lock);
			target_r_iter=rb.pending_req->find(param->value->ppa/L2PGAP);
			for(;target_r_iter->first==param->value->ppa/L2PGAP && 
					target_r_iter!=rb.pending_req->end();){
				pending_req=target_r_iter->second;
				processing_pending_req(pending_req, param->value);
				rb.pending_req->erase(target_r_iter++);
			}
			rb.issue_req->erase(param->value->ppa/L2PGAP);
			fdriver_unlock(&rb.pending_lock);

			fdriver_lock(&rb.read_buffer_lock);
			rb.buffer_ppa=param->value->ppa/L2PGAP;
			memcpy(rb.buffer_value, param->value->value, PAGESIZE);
			fdriver_unlock(&rb.read_buffer_lock);

			if(param->value->ppa%L2PGAP){
				memmove(param->value->value, &param->value->value[(param->value->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
			}

			break;
	}
	request *res=input->parents;
	if(res){
		res->type_ftl=res->type_lower=0;
		res->end_req(res);//you should call the parents end_req like this
	}
	free(param);
	free(input);
	return NULL;
}

inline uint32_t xx_to_byte(char *a){
	switch(a[0]){
		case 'K':
			return 1024;
		case 'M':
			return 1024*1024;
		case 'G':
			return 1024*1024*1024;
		default:
			break;
	}
	return 1;
}

uint32_t page_argument(int argc, char **argv){
	bool cache_size;
	uint32_t len;
	int c;
	char temp;
	uint32_t base;
	uint32_t value;
	while((c=getopt(argc,argv,"c"))!=-1){
		switch(c){
			case 'c':
				cache_size=true;
				len=strlen(argv[optind]);
				temp=argv[optind][len-1];
				if(temp < '0' || temp >'9'){
					argv[optind][len-1]=0;
					base=xx_to_byte(&temp);
				}
				value=atoi(argv[optind]);
				value*=base;
				break;
			default:
				break;
		}
	}
	return 1;
}

#if 1 //NAM 
uint8_t is_page_dirty(uint32_t idx){ 
	pm_body *p=(pm_body*)page_ftl.algo_body; 

	if(p->dirty_check[idx] & dirty_option)
		return 1; 

	return 0; 
} 

void pageIsDirty(uint32_t idx){ 
	pm_body *p=(pm_body*)page_ftl.algo_body; 

	if(p->dirty_check[idx] & node_dirty_option)
		return; 

//	p->dirty_check[idx] |= node_dirty_option; 
	node_dirty* newNode = (node_dirty*)malloc(sizeof(node_dirty)); 
	newNode->idx = idx; 
	newNode->next = dHead; 
	dHead = newNode; 

	return; 
} 

void pageIsClean(uint32_t idx){
	if(d_buffer.htable[idx].count != 0){ 
		search_heap(idx); 
	} 
	else{
		element newElement; 

		newElement.idx = idx; 
		newElement.count = 1; 
		
		insert_max_heap(newElement); 
	}
	return; 
} 
#endif

#if 1 //NAM
void insert_max_heap(element element){ 
	int i; 
	i = ++(h.heap_size); 
	
	while((i != 1) && (element.count > h.heap[i/2].count)){ 
		h.heap[i] = h.heap[i/2]; 
		i /= 2; 
	} 
	h.heap[i] = element; 

	return; 
} 

void search_heap(uint32_t idx){ 
	uint64_t i = 1; 
	element tmp; 
	
	while(h.heap[i].idx != idx)
		i++; 

	h.heap[i].count++; 

	if(h.heap[i].count > h.heap[i/2].count){ 
		tmp = h.heap[i]; 
		
		while((i != 1) && (h.heap[i].count > h.heap[i/2].count)){ 
			h.heap[i] = h.heap[i/2];
			i /= 2; 
		} 
		h.heap[i] = tmp; 	
	}	
	return; 
} 

void delete_max_heap(){ 
	uint32_t parent, child; 
	element item, temp; 
	
	item = h.heap[1]; 
	temp = h.heap[(h.heap_size)--]; 
	parent = 1; 
	child = 2; 
	
	while(child <= h.heap_size){ 
		if((child < h.heap_size) && ((h.heap[child].count) < h.heap[child + 1].count))
			child++; 
	
		if(temp.count >= h.heap[child].count) 
			break; 

		h.heap[parent] = h.heap[child]; 
		parent = child; 
		child *= 2; 
	} 
	h.heap[parent] = temp; 
	return; 
} 
#endif




























