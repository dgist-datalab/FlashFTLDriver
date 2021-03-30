#include "sst_block_file_stream.h"
#include <stdlib.h>
extern uint32_t debug_lba;
sst_bf_out_stream *sst_bos_init(bool (*r_check_done)(inter_read_alreq_param *, bool), bool no_inter_param_alloc){
	sst_bf_out_stream *res=(sst_bf_out_stream*)calloc(1, sizeof(sst_bf_out_stream));
	res->kv_read_check_done=r_check_done;
	res->kv_wrapper_q=new std::queue<key_value_wrapper*>();
	res->prev_ppa=UINT32_MAX;
	res->prev_lba=UINT32_MAX;
	res->no_inter_param_alloc=no_inter_param_alloc;
	return res;
}

static key_value_wrapper *kv_wrapper_setting(sst_bf_out_stream *bos, key_value_wrapper **kv_wrap_buf, 
		uint8_t num, compaction_master *cm){
	key_value_wrapper *res;
	char *value;
	for(uint32_t i=0; i<num; i++){
		if(i==0){
			res=kv_wrap_buf[i];
			if(!bos->no_inter_param_alloc){
				res->param=compaction_get_read_param(cm);
			}
			else{
				res->param=(inter_read_alreq_param*)calloc(1, sizeof(inter_read_alreq_param));
			}
	//		printf("after size:%u\n", cm->read_param_queue->size());
			res->param->data=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
			fdriver_lock_init(&res->param->done_lock, 0);
			value=res->param->data->value;
		}
		kv_wrap_buf[i]->kv_ptr.data=&value[LPAGESIZE*(kv_wrap_buf[i]->piece_ppa%L2PGAP)];
	}
	return res;
}

key_value_wrapper *sst_bos_add(sst_bf_out_stream *bos, 
		key_value_wrapper *kv_wrapper, compaction_master *cm){

	if(bos->prev_lba==UINT32_MAX){
		bos->prev_lba=kv_wrapper->kv_ptr.lba;
	}
	else{
		if(bos->prev_lba>=kv_wrapper->kv_ptr.lba){
			EPRINT("lba shoud be inserted by increasing order", true);
		}
		bos->prev_lba=kv_wrapper->kv_ptr.lba;
	}

	key_value_wrapper *res=NULL;
	bos->kv_wrapper_q->push(kv_wrapper);

	if(bos->prev_ppa!=UINT32_MAX && bos->prev_ppa==PIECETOPPA(kv_wrapper->piece_ppa)){
		bos->kv_wrap_buffer[bos->kv_buf_idx++]=kv_wrapper;
		if(bos->kv_buf_idx==L2PGAP){
			res=kv_wrapper_setting(bos, bos->kv_wrap_buffer, bos->kv_buf_idx, cm);
			bos->kv_buf_idx=0;
			bos->prev_ppa=UINT32_MAX;
			return res;
		}
	}
	else{
		if(bos->prev_ppa!=UINT32_MAX){
			res=kv_wrapper_setting(bos ,bos->kv_wrap_buffer, bos->kv_buf_idx, cm);
		}
		bos->kv_buf_idx=0;
		bos->prev_ppa=PIECETOPPA(kv_wrapper->piece_ppa);
		bos->kv_wrap_buffer[bos->kv_buf_idx++]=kv_wrapper;
		return res;
	}

	return res;
}

key_value_wrapper *sst_bos_get_pending(sst_bf_out_stream *bos, 
		compaction_master *cm){
	if(bos->kv_buf_idx){
		key_value_wrapper *res=kv_wrapper_setting(bos, bos->kv_wrap_buffer, bos->kv_buf_idx, cm);
		bos->kv_buf_idx=0;
		return res;
	}
	else return NULL;
}

key_value_wrapper* sst_bos_pick(sst_bf_out_stream * bos, bool should_buffer_check){
	key_value_wrapper *target=bos->kv_wrapper_q->front();
	if(!bos->now_kv_wrap){
	//	bos->now_kv_wrap=(key_value_wrapper*)malloc(sizeof(key_value_wrapper));
		bos->now_kv_wrap=target;
	}

	if(target->param){
		bos->kv_read_check_done(target->param, false);
	}
	else{
		if(should_buffer_check){
			for(uint32_t i=0; i<bos->kv_buf_idx; i++){
				if(bos->kv_wrap_buffer[i]==target){
					return NULL;
				}
			}
		}
	}

	if(target->kv_ptr.data==NULL){
		EPRINT("over entry pick!", true);
	}
	return target;
}

void sst_bos_pop(sst_bf_out_stream *bos, compaction_master *cm){
	key_value_wrapper *target=bos->kv_wrapper_q->front();
	if(PIECETOPPA(bos->now_kv_wrap->piece_ppa)!=PIECETOPPA(target->piece_ppa)){
		inf_free_valueset(bos->now_kv_wrap->param->data, FS_MALLOC_R);
		if(!bos->no_inter_param_alloc){
			compaction_free_read_param(cm, bos->now_kv_wrap->param);
		}
		else{
			free(bos->now_kv_wrap->param);
		}
		if(!target->param){
			EPRINT("can't be", true);
		}
		free(bos->now_kv_wrap);
		bos->now_kv_wrap=target;
	}
	else{
		if(target!=bos->now_kv_wrap){
			free(target);
		}
	}
	bos->kv_wrapper_q->pop();
}

void sst_bos_pop_pending(sst_bf_out_stream *bos, struct compaction_master *cm){
	key_value_wrapper *target=bos->kv_wrapper_q->front();
	if(PIECETOPPA(bos->now_kv_wrap->piece_ppa)!=PIECETOPPA(target->piece_ppa)){
		inf_free_valueset(bos->now_kv_wrap->param->data, FS_MALLOC_R);
		compaction_free_read_param(cm, bos->now_kv_wrap->param);
		bos->now_kv_wrap=NULL;
	}
}

bool sst_bos_is_empty(sst_bf_out_stream *bos){
	return bos->kv_wrapper_q->size()==0;
}

void sst_bos_free(sst_bf_out_stream *bos, compaction_master *cm){
	if(bos->kv_buf_idx){
		EPRINT("free after processing pending req", true);
	}
	inf_free_valueset(bos->now_kv_wrap->param->data, FS_MALLOC_R);
	compaction_free_read_param(cm, bos->now_kv_wrap->param);
	free(bos->now_kv_wrap);
	delete bos->kv_wrapper_q;
	free(bos);
}

sst_bf_in_stream * sst_bis_init(uint32_t start_piece_ppa, uint32_t piece_ppa_length, page_manager*pm,
		bool make_read_helper, read_helper_param rhp){
	sst_bf_in_stream *res=(sst_bf_in_stream*)calloc(1, sizeof(sst_bf_in_stream));
	res->prev_lba=UINT32_MAX;
	res->start_lba=UINT32_MAX;
	res->start_piece_ppa=start_piece_ppa;
	res->piece_ppa_length=piece_ppa_length;
	res->map_data=new std::queue<value_set*>();
	res->pm=pm;
	res->buffer=(key_value_wrapper*)malloc(L2PGAP*sizeof(key_value_wrapper));

	res->make_read_helper=make_read_helper;

	if(make_read_helper){	
		res->rh=read_helper_init(rhp);
		res->rhp=rhp;
	}
	return res;
}

bool sst_bis_insert(sst_bf_in_stream *bis, key_value_wrapper *kv_wrapper){
	if(bis->buffer_idx==L2PGAP){
		EPRINT("plz check buffer_idx", true);
	}
	bis->buffer[bis->buffer_idx++]=*kv_wrapper;

	if(bis->start_lba>kv_wrapper->kv_ptr.lba){
		bis->start_lba=kv_wrapper->kv_ptr.lba;
	}
	if(bis->end_lba<kv_wrapper->kv_ptr.lba){
		bis->end_lba=kv_wrapper->kv_ptr.lba;
	}

	if(bis->prev_lba==UINT32_MAX){
		bis->prev_lba=kv_wrapper->kv_ptr.lba;
	}
	else{
		if(bis->prev_lba>=kv_wrapper->kv_ptr.lba){
			EPRINT("lba shoud be inserted by increasing order", true);
		}
		bis->prev_lba=kv_wrapper->kv_ptr.lba;
	}

	if(bis->buffer_idx==L2PGAP){
		 return true;
	}
	else return false;
}

value_set* sst_bis_finish(sst_bf_in_stream*){
	EPRINT("not implemented", true);
	return NULL;
}

extern char all_set_page[PAGESIZE];

value_set* sst_bis_get_result(sst_bf_in_stream *bis, bool last, uint32_t *debug_idx, key_ptr_pair *debug_kp){ 
	//it should return unaligned value when it no space

	if(!last && REMAIN_DATA_PPA(bis)==bis->buffer_idx){
		//EPRINT("change bis", false);
	}
	else if(bis->buffer_idx!=L2PGAP && !sst_bis_ppa_empty(bis) && !last){
		EPRINT("wtf??", true);
		return NULL;
	}
	else if(last && REMAIN_DATA_PPA(bis)< bis->buffer_idx){
		EPRINT("plz update new bis", true);
		return NULL;
	}


	value_set *res=inf_get_valueset(NULL, FS_MALLOC_W, PAGESIZE);
	uint32_t ppa=page_manager_get_new_ppa(bis->pm, false, DATASEG);
	if(ppa/_PPS != bis->start_piece_ppa/2/_PPS){
		EPRINT("new data should same segment", true);
	}
	res->ppa=ppa;
	for(uint32_t i=0; i<bis->buffer_idx;i++){
		if(bis->now_map_data_idx==KP_IN_PAGE){
			bis->now_map_data=NULL;
		}

		if(!bis->now_map_data){
			bis->now_map_data=inf_get_valueset(all_set_page, FS_MALLOC_W, PAGESIZE);
			bis->map_data->push(bis->now_map_data);	
			bis->now_map_data_idx=0;
		}

		key_ptr_pair *map_pair=&((key_ptr_pair*)(bis->now_map_data->value))[bis->now_map_data_idx++];
		map_pair->lba=bis->buffer[i].kv_ptr.lba;
		map_pair->piece_ppa=ppa*L2PGAP+(i%L2PGAP);

		debug_kp[i].lba=map_pair->lba;
		debug_kp[i].piece_ppa=map_pair->piece_ppa;

		if(map_pair->lba==debug_lba){
			printf("tiering lba:%u map to %u\n", debug_lba, map_pair->piece_ppa);
		}
		validate_piece_ppa(bis->pm->bm, 1, &map_pair->piece_ppa, &map_pair->lba, true);
		memcpy(&res->value[(i%L2PGAP)*LPAGESIZE], bis->buffer[i].kv_ptr.data, LPAGESIZE);

		if(bis->make_read_helper){
			read_helper_stream_insert(bis->rh, map_pair->lba, map_pair->piece_ppa);
		}

		bis->write_issued_kv_num++;
		//free(bis->buffer[i]);
	}

	(*debug_idx)=bis->buffer_idx;
	bis->buffer_idx=0;
	return res;
}

bool sst_bis_ppa_empty(sst_bf_in_stream *bis){
	uint32_t remain_ppa=REMAIN_DATA_PPA(bis);
	if(remain_ppa<0){
		EPRINT("can't be!", true);
	}
	if(remain_ppa==0 && bis->buffer_idx){
		EPRINT("it need to flush!", false);
	}
	return remain_ppa==0;
}

void sst_bis_free(sst_bf_in_stream *bis){
	if(bis->rh){
		EPRINT("make rh null before free", true);
	}

	if(!bis->map_data->empty()){
		EPRINT("not flush map data", true);	
	}
	
	if(bis->buffer_idx){
		EPRINT("remain data", true);	
	}
	
	delete bis->map_data;
	free(bis->buffer);
	free(bis);
}

