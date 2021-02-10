#include "io.h"
io_manager io_m;
void io_manager_init(lower_info *li){
	io_m.li=li;
	io_m.tm=tag_manager_init(QSIZE);

	for(uint32_t i=0; i<QSIZE; i++){
//		fdriver_mutex_init(&io_m.sync_mutex[i]);
		fdriver_lock_init(&io_m.sync_mutex[i], 0);
	}
	memset(io_m.wrapper, 0, sizeof(sync_wrapper)*QSIZE);
}

static inline void *io_sync_end_req(algo_req* al_req){
	sync_wrapper *wrapper=(sync_wrapper*)al_req->params;
	al_req->end_req=wrapper->end_req;
	al_req->params=wrapper->params;
	uint32_t tag=wrapper->tag;
	
	al_req->end_req(al_req);
	fdriver_unlock(&io_m.sync_mutex[tag]);
	tag_manager_free_tag(io_m.tm, tag);
	return NULL;
}

static inline void set_wrapper(sync_wrapper *wrapper, algo_req *al_req, uint32_t tag){
	wrapper->tag=tag;
	wrapper->end_req=al_req->end_req;
	wrapper->params=al_req->params;
	al_req->end_req=io_sync_end_req;
}

void io_manager_issue_internal_write(uint32_t ppa, value_set *value, algo_req *al_req, bool sync){
	if(sync){
		uint32_t tag=tag_manager_get_tag(io_m.tm);
		sync_wrapper *wrapper=&io_m.wrapper[tag];
		set_wrapper(wrapper, al_req, tag);
		io_m.li->write(ppa, PAGESIZE ,value, ASYNC, al_req);
		fdriver_lock(&io_m.sync_mutex[tag]);
	}
	else{
		io_m.li->write(ppa, PAGESIZE ,value, ASYNC, al_req);
	}
}

void io_manager_issue_internal_read(uint32_t ppa, value_set *value, algo_req *al_req, bool sync){
	if(sync){
		uint32_t tag=tag_manager_get_tag(io_m.tm);
		sync_wrapper *wrapper=&io_m.wrapper[tag];
		set_wrapper(wrapper, al_req, tag);
		io_m.li->read(ppa, PAGESIZE ,value, ASYNC, al_req);
		fdriver_lock(&io_m.sync_mutex[tag]);
	}else{
		io_m.li->read(ppa, PAGESIZE ,value, ASYNC, al_req);
	}
}

void io_manager_issue_write(uint32_t ppa, value_set *value, algo_req *al_req, bool sync){
	io_m.li->write(ppa, PAGESIZE ,value, ASYNC, al_req);
}

void io_manager_issue_read(uint32_t ppa, value_set *value, algo_req *al_req, bool sync){
	io_m.li->read(ppa, PAGESIZE ,value, ASYNC, al_req);
}

void io_manager_free(){
	tag_manager_free_manager(io_m.tm);
}
