#include "./block_buffer_write.h"
#include "../../bench/measurement.h"
#define NOT_ASSIGNED_TAG UINT32_MAX 
extern AmfManager *am;
bool stopflag;
physical_block_buffer buf_array[AMF_PUNIT];
void *p_bbuf_main(void *);
static pthread_t blk_tid;

extern void amf_error_call_back_r(void *req);
extern void amf_error_call_back_w(void *req);
extern void amf_error_call_back_e(void *req);

void p_bbuf_call_back_io(void *req);
void p_bbuf_call_back_e(void *req);

typedef struct debug_buffer{
	uint32_t not_idle_cnt;
	uint32_t no_data_cnt;
	MeasureTime amf_time;
}debug_buffer;

static debug_buffer monitor;

void p_bbuf_init(){
	SetReadCb(am,  p_bbuf_call_back_io, amf_error_call_back_r);
	SetWriteCb(am, p_bbuf_call_back_io, amf_error_call_back_w);
	SetEraseCb(am, p_bbuf_call_back_e, amf_error_call_back_e);

	measure_init(&monitor.amf_time);

	physical_block_buffer *t_buf;
	for(uint32_t i=0; i<AMF_PUNIT; i++){
		t_buf=&buf_array[i];
		t_buf->idx=i;
		fdriver_lock_init(&t_buf->idle_check_lock, 1);
		fdriver_mutex_init(&t_buf->schedule_lock);
		t_buf->tag=tag_manager_init(RPPB);
		for(uint32_t j=0; j<RPPB; j++){
			t_buf->req[j].org_req=NULL;
			t_buf->req[j].org_param=NULL;
			t_buf->req[j].org_end_req=NULL;
			t_buf->req[j].tag_num=j;
			t_buf->req[j].buf=t_buf;
		}

		t_buf->rp_tag=tag_manager_init(_PPB);
		memset(t_buf->return_param_array, 0, sizeof(t_buf->return_param_array));
		for(uint32_t j=0; j<_PPB; j++){
			fdriver_mutex_init(&t_buf->return_param_array[j].lock);
		}
	}

	pthread_create(&blk_tid, NULL, p_bbuf_main, NULL);
}

static inline void __free_wrapper(physical_block_buffer *t_buf, 
		algo_req_wrapper *target, uint32_t tag_num){
	target->org_req=NULL;
	target->org_param=NULL;
	target->org_end_req=NULL;
	if(tag_num==UINT32_MAX){
		GDB_MAKE_BREAKPOINT;
	}
	tag_manager_free_tag(t_buf->tag, tag_num);
}

static inline void __initialize_wrapper(algo_req_wrapper *target, uint32_t type, 
		uint32_t rppa, char *data, return_param *rp, algo_req *req){
	target->type=type;
	target->rppa=rppa;
	target->data=data;
	target->org_req=req;
	target->rp=rp;
	if(req){
		target->org_param=req->param;
		target->org_end_req=req->end_req;
	}
	//req->param=(void*)target;
}

static inline return_param * __get_return_param(physical_block_buffer *t_buf, bool sync){
	uint32_t tag=tag_manager_get_tag(t_buf->rp_tag);
	return_param *res=&t_buf->return_param_array[tag];
	res->tag=tag;
	res->sync=sync;
	res->cnt=0;
	if(res->sync){
		fdriver_lock(&res->lock);
	}
	return res;
}

static inline void __free_return_param(physical_block_buffer* t_buf, return_param *rp){
	if(rp->sync){
		fdriver_unlock(&rp->lock);
	}
	//printf("t_buf:%u rp_tag:%u\n",t_buf->idx, rp->tag);
	tag_manager_free_tag(t_buf->rp_tag, rp->tag);
}

static inline uint32_t reset_ppa(uint32_t ppa, uint32_t piece_offset){
	uint32_t seg_idx=(ppa*R2PGAP)/(1<<15);
	uint32_t punit=BUF_IDX(ppa);
	uint32_t page=REAL_PPA(ppa, piece_offset);
	uint32_t res=seg_idx*(1<<15) | page *AMF_PUNIT | punit;
	/*
	static int cnt=0;
	printf("[%d]seg_idx:%u, punit:%u, page:%u -> res:%u\n", cnt++, seg_idx, punit, page, res);*/
	
	return res;
}

static inline void __p_bbuf_issue(physical_block_buffer *t_buf, uint32_t type, uint32_t ppa, char *data,
		return_param* rp,uint32_t loop_cnt, algo_req *input){
	for(uint32_t i=0; i<loop_cnt; i++){
		uint32_t target_tag=tag_manager_get_tag(t_buf->tag);
		uint32_t rppa=reset_ppa(ppa, i);
		__initialize_wrapper(&t_buf->req[target_tag], type, rppa, 
				&data[i*REAL_PAGE_SIZE], rp, input);

		fdriver_lock(&t_buf->schedule_lock);
		t_buf->schedule_queue.push_back(target_tag);
		fdriver_unlock(&t_buf->schedule_lock);
	}
}

void p_bbuf_issue(uint32_t type, uint32_t ppa, char *data, algo_req *input){
	uint32_t loop_cnt=(type==LOWER_TRIM?1:R2PGAP);
	uint32_t target_buf_idx=BUF_IDX(ppa);
	physical_block_buffer *t_buf=&buf_array[target_buf_idx];
	return_param *rp=__get_return_param(t_buf, false);

	__p_bbuf_issue(t_buf,type, ppa, data, rp, loop_cnt, input);
}


void p_bbuf_sync_issue(uint32_t type, uint32_t ppa, char *data){
	uint32_t loop_cnt=(type==LOWER_TRIM?1:R2PGAP);
	uint32_t target_buf_idx=BUF_IDX(ppa);
	physical_block_buffer *t_buf=&buf_array[target_buf_idx];
	return_param *rp=__get_return_param(t_buf, true);

	__p_bbuf_issue(t_buf, type, ppa, data, rp, loop_cnt, NULL);
	fdriver_lock(&rp->lock);
	__free_return_param(t_buf, rp);
}

void p_bbuf_join(){
	stopflag=true;
	pthread_join(blk_tid, NULL);
}

void p_bbuf_free(){
	p_bbuf_join();
	printf("NI:ND %u:%u\n",monitor.not_idle_cnt, monitor.no_data_cnt);
	/*
	for(uint32_t i=0; i< AMF_PUNIT; i++){
		tag_manager_wait(buf_array[i].tag);
		tag_manager_free_manager(buf_array[i].tag);
		tag_manager_wait(buf_array[i].rp_tag);
		tag_manager_free_manager(buf_array[i].rp_tag);
	}*/
}

void *p_bbuf_main(void *){
	uint32_t now_buf=0;
	algo_req_wrapper *t_req;
	while(1){
		if(stopflag){
			bool out_flag=true;
			for(uint32_t i=0; i<AMF_PUNIT; i++){
				physical_block_buffer *t_buf=&buf_array[i];
				fdriver_lock(&t_buf->schedule_lock);
				if(!t_buf->schedule_queue.empty()){
					out_flag=true;
					fdriver_unlock(&t_buf->schedule_lock);
					break;
				}
				fdriver_unlock(&t_buf->schedule_lock);
			}

			if(out_flag) break;
		}
		physical_block_buffer *t_buf=&buf_array[now_buf++%AMF_PUNIT];
		if(t_buf->schedule_queue.empty()){ 	
	//		printf("no data continue\n");
			monitor.no_data_cnt++;
			continue;
		}
		int value=0;
		fdriver_getvalue(&t_buf->idle_check_lock, &value);
		if(value==0){
		//	printf("not idle continue\n");
			monitor.not_idle_cnt++;
			continue;
		}
		fdriver_lock(&t_buf->idle_check_lock);

		fdriver_lock(&t_buf->schedule_lock);
		std::list<uint32_t>::iterator iter=t_buf->schedule_queue.begin();
		uint32_t target_tag=*iter;
		t_req=&t_buf->req[target_tag];
		/*
		char temp[255]={0,};
		sprintf(temp, "%u-%d-%lu:", t_buf->idx, value, t_buf->schedule_queue.size());
		measure_start(&monitor.amf_time);*/
		switch(t_req->type){
			case LOWER_WRITE:
#ifdef TESTING
				p_bbuf_call_back_io((void*)t_req);
#else
				AmfWrite(am, t_req->rppa, t_req->data, (void*)t_req);
#endif
				break;
			case LOWER_READ:
#ifdef TESTING
				p_bbuf_call_back_io((void*)t_req);
#else
				AmfWrite(am, t_req->rppa, t_req->data, (void*)t_req);
#endif
				break;
			case LOWER_TRIM:
#ifdef TESTING
				p_bbuf_call_back_e((void*)t_req);
#else
				AmfErase(am, t_req->rppa, (void*)t_req);
#endif
				break;
		}
		//measure_end(&monitor.amf_time, temp);
		t_buf->schedule_queue.erase(iter);
		fdriver_unlock(&t_buf->schedule_lock);
	}

	return NULL;
}

void p_bbuf_call_back_io(void *req){
	algo_req_wrapper *t_req=(algo_req_wrapper*)req;
	return_param *rp=t_req->rp;
	if(++rp->cnt==R2PGAP){
		algo_req *org_req=t_req->org_req;
		/*
		static uint32_t cnt=0;
		printf("return cnt:%u\n", cnt++);*/
		if(org_req){
			org_req->param=t_req->org_param;
			org_req->end_req=t_req->org_end_req;
			org_req->end_req(org_req);
		}
		if(rp->sync){
			fdriver_unlock(&rp->lock);
		}else{
			__free_return_param(t_req->buf,rp);
		}
	}
	fdriver_unlock(&t_req->buf->idle_check_lock);
	__free_wrapper(t_req->buf, t_req, t_req->tag_num);
}

void p_bbuf_call_back_e(void *req){
	algo_req_wrapper *t_req=(algo_req_wrapper*)req;
	__free_return_param(t_req->buf, t_req->rp);
	fdriver_unlock(&t_req->buf->idle_check_lock);
	__free_wrapper(t_req->buf, t_req, t_req->tag_num);
}
