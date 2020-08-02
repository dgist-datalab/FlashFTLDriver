#include "amf_info.h"
AmfManager *am;

void amf_call_back(void *req);
void amf_error_call_back(void *req);

typedef struct dummy_req{
	uint8_t type;
}dummy_req;

lower_info amf_info={
	.create=amf_info_create,
	.destroy=amf_info_destroy,
	.write=amf_info_push_data,
	.read=amf_info_pull_data,
	.read_hw=amf_info_hw_read,
	.device_badblock_checker=NULL,
	.trim_block=amf_info_trim_block,
	.trim_a_block=NULL,
	.refresh=amf_info_refresh,
	.stop=amf_info_stop,
	.lower_alloc=NULL,
	.lower_free=NULL,
	.lower_flying_req_wait=amf_flying_req_wait,
	.lower_show_info=amf_show_info_,

	.lower_tag_num=amf_info_lower_tag_num,
	.hw_do_merge=NULL,
	.hw_get_kt=NULL,
	.hw_get_inv=NULL
}

static uint8_t test_type(uint8_t type){
	uint8_t t_type=0xff>>1;
	return type&t_type;
}

uint32_t amf_info_create(lower_info *li, blockmanager *bm){
	am=AmfOpen(2);

	SetReadCb(am, amf_call_back, amf_error_call_back);
	SetWriteCb(am, amf_call_back, amf_error_call_back);
	SetEraseCb(am, amf_call_back, amf_error_call_back);

	bm->create(bm, &amf_info);
}

void* amf_info_destroy(lower_info *li){
	amf_flying_req_wait();

	for(int i=0; i<LREQ_TYPE_NUM;i++){
		fprintf(stderr,"%s %lu\n",bench_lower_type(i),li->req_type_cnt[i]);
	}

    fprintf(stderr,"Total Read Traffic : %lu\n", li->req_type_cnt[1]+li->req_type_cnt[3]+li->req_type_cnt[5]+li->req_type_cnt[7]);
    fprintf(stderr,"Total Write Traffic: %lu\n\n", li->req_type_cnt[2]+li->req_type_cnt[4]+li->req_type_cnt[6]+li->req_type_cnt[8]);
    fprintf(stderr,"Total WAF: %.2f\n\n", (float)(li->req_type_cnt[2]+li->req_type_cnt[4]+li->req_type_cnt[6]+li->req_type_cnt[8]) / li->req_type_cnt[6]);

	li->write_op=li->read_op=li->trim_op=0;
	AmfClose(am);
}

void* amf_info_write(uint32_t ppa, uint32_t size, value_set *value,bool async,algo_req * const req){
	
	uint8_t t_type=test_type(req->type);
	if(t_type < LREQ_TYPE_NUM){
		memio_info.req_type_cnt[t_type]++;
	}

	AmfRead(am, ppa, value->value, (void *)req);
	return NULL;
}


void* amf_info_read(uint32_t ppa, uint32_t size, value_set *value,bool async,algo_req * const req){
	uint8_t t_type=test_type(req->type);
	if(t_type < LREQ_TYPE_NUM){
		memio_info.req_type_cnt[t_type]++;
	}

	AmfRead(am, ppa, value->value, (void *)req);
	return NULL;
}

void* amf_info_trim_block(uint32_t ppa,bool async){
	dummy_req *temp=(dummy_req*)malloc(sizeof(dummy_req));
	memio_info.req_type_cnt[TRIM]++;
	temp->type=TRIM;
	AmfErase(am,ppa,(void*)temp);
	return NULL;
}


void* amf_info_refresh(struct lower_info*){
	li->write_op=li->read_op=li->trim_op=0;
	return NULL;
}

void amf_info_stop(){}

void amf_info_lower_flying_req_wait(){}

void amf_info_lower_show_info(){}

uint32_t amf_info_lower_tag_num(){
	return NUM_TAGS;
}
