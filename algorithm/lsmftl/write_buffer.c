#include "../../interface/interface.h"
#include "lftl_slab.h"
#include "write_buffer.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>
write_buffer *write_buffer_reinit(write_buffer *wb){
	std::map<uint32_t, buffer_entry*>::iterator it=wb->data->begin();
	for(;it!=wb->data->end(); it++){
		slab_free(wb->sm,(char*)it->second);
	}
	wb->data->clear();
	wb->buffered_entry_num=0;
	return wb;
}

write_buffer *write_buffer_init(uint32_t max_buffered_entry_num, page_manager *pm){
	write_buffer *res=(write_buffer*)calloc(1, sizeof(write_buffer));
	res->data=new std::map<uint32_t, buffer_entry*>();
	res->max_buffer_entry_num=max_buffered_entry_num;
	//res->data=(buffer_entry**)calloc(max_buffered_entry_num,sizeof(buffer_entry));
	res->sm=slab_master_init(sizeof(buffer_entry), max_buffered_entry_num);
	res->pm=pm;
	fdriver_mutex_init(&res->cnt_lock);
	fdriver_mutex_init(&res->sync_lock);
	res->flushed_req_cnt=0;
	return res;
}

typedef struct flush_req_param{
	value_set *value;
	uint32_t *cnt;
	fdriver_lock_t *cnt_lock;
	fdriver_lock_t *sync_lock;
}flush_req_param;

static void *flush_end_req(algo_req *req){
	flush_req_param *param=(flush_req_param*)req->param;
	value_set *value=(value_set*)param->value;
	if(param->sync_lock){
		fdriver_lock(param->sync_lock);
		(*param->cnt)--;
		if(!(*param->cnt)){
			fdriver_unlock(param->sync_lock);
		}
		fdriver_unlock(param->sync_lock);
	}
	inf_free_valueset(value, FS_MALLOC_W);
	free(req);
	return NULL;
}

static algo_req *make_flush_algo_req(write_buffer *wb,uint32_t ppa, value_set *value, bool sync){
	algo_req *res=(algo_req*)malloc(sizeof(algo_req));
	flush_req_param *param=(flush_req_param*)malloc(sizeof(flush_req_param));
	res->type=DATAW;
	res->ppa=ppa;
	res->end_req=flush_end_req;
	param->value=value;
	if(sync){
		param->cnt_lock=&wb->cnt_lock;
		param->sync_lock=&wb->sync_lock;
		fdriver_lock(param->cnt_lock);
		param->cnt=&wb->flushed_req_cnt;
		wb->flushed_req_cnt++;	
		fdriver_unlock(param->cnt_lock);
	}
	else{
		param->cnt_lock=param->sync_lock=NULL;
	}

	res->param=(void*)param;
	return res;
}

static inline uint32_t number_of_write_page(write_buffer *wb){
	return wb->buffered_entry_num/L2PGAP + (wb->buffered_entry_num%L2PGAP?1:0);
}

key_ptr_pair* write_buffer_flush(write_buffer *wb, bool sync){
	std::map<uint32_t, buffer_entry*>::iterator it=wb->data->begin();
	value_set* target_value=NULL;
	uint32_t ppa=-1;
	char *oob=NULL;
	key_ptr_pair *res=(key_ptr_pair*)malloc(sizeof(key_ptr_pair)*wb->data->size());
	for(uint32_t i=0; it!=wb->data->end(); i++, it++){
		uint8_t inter_idx=i%L2PGAP;
		if(inter_idx==0){
			ppa=page_manager_get_new_ppa(wb->pm, false);
			target_value=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
			target_value->ppa=ppa;
			oob=wb->pm->bm->get_oob(wb->pm->bm, ppa);
		}	

		//page_manager_insert_lba(wb->pm, it->first);
		memcpy(&target_value->value[inter_idx*LPAGESIZE], it->second->data->value, LPAGESIZE);//copy value
		*(uint32_t*)&oob[sizeof(uint32_t)*inter_idx]=it->first;//copy lba to oob
		res[i].piece_ppa=ppa*L2PGAP+inter_idx;
		res[i].lba=it->first;

		if(inter_idx==(L2PGAP-1)){//issue data
			io_manager_issue_internal_write(ppa, target_value, make_flush_algo_req(wb, ppa, target_value, sync), false);
			ppa=-1;
			oob=NULL;
			target_value=NULL;
		}
	}

	if(sync){
		fdriver_lock(&wb->sync_lock);
	}	

	return res;
}

uint32_t write_buffer_insert(write_buffer *wb, uint32_t lba, value_set* value){
	if(wb->is_immutable){
		return encode_return_code(FAILED, WB_IS_IMMUTABLE);
	}

	std::map<uint32_t, buffer_entry*>::iterator it;
	it=wb->data->find(lba);
	if(!wb->data->size() || it==wb->data->end()){/*new data*/
		buffer_entry *ent=(buffer_entry*)slab_alloc(wb->sm);
		ent->lba=lba;
		ent->data=value;
		wb->data->insert(std::pair<uint32_t, buffer_entry*>(lba, ent));
		wb->buffered_entry_num++;
	}
	else{/*update data*/
		buffer_entry *obsolete=wb->data->at(lba);
		inf_free_valueset(obsolete->data, FS_MALLOC_W);
		obsolete->data=value;
		wb->data->at(lba)=obsolete;
	}

	if(wb->buffered_entry_num==wb->max_buffer_entry_num){
		return encode_return_code(SUCCESSED, WB_FULL);
	}
	else{
		return encode_return_code(SUCCESSED, WB_NONE);
	}
}

char *write_buffer_get(write_buffer *wb, uint32_t lba){
	std::map<uint32_t, buffer_entry*>::iterator it=wb->data->find(lba);
	if(it==wb->data->end()){
		return NULL;
	}
	else{
		return it->second->data->value;
	}
}

void write_buffer_free(write_buffer* wb){
	delete wb->data;
	slab_master_free(wb->sm);
	fdriver_destroy(&wb->cnt_lock);
	fdriver_destroy(&wb->sync_lock);
	free(wb);
}
