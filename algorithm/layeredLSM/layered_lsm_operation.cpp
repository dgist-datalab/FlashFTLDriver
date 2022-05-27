#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./lsmtree.h"

uint32_t operation_temp(request *const ){return 1;}
uint32_t remove_temp(request *const );
uint32_t print_log_temp();

uint32_t create_temp(lower_info *,blockmanager *, struct algorithm *);
void destroy_temp(lower_info *, struct algorithm *);
uint32_t write_temp(request *const );
uint32_t read_temp(request *const );
uint32_t test_function();

uint32_t algo_dump(FILE *fp);
uint32_t algo_load(lower_info *li, blockmanager *bm, struct algorithm *al, FILE *fp);
struct algorithm layered_lsm={
	.argument_set=lsmtree_argument_set,
	.create=create_temp,
	.destroy=destroy_temp,
	.read=read_temp,
	.write=write_temp,
	.flush=operation_temp,
	.remove=remove_temp,
	.test=test_function,
	.print_log=print_log_temp,
	.empty_cache=NULL,
	.dump_prepare=NULL,
	.dump=algo_dump,
	.load=algo_load,
};

char all_set_data[PAGESIZE];

lsmtree *LSM;
lsmtree_parameter *target_param;
lower_info *g_li;

uint32_t create_temp(lower_info *li,blockmanager *sm, struct algorithm *){
	g_li=li;
	if(!target_param){
		target_param=(lsmtree_parameter*)calloc(1, sizeof(lsmtree_parameter));
		lsmtree_parameter default_param=lsmtree_calculate_parameter(0.1, bit_calculate(RANGE), 48*RANGE/100*30, RANGE);
		memcpy(target_param, &default_param, sizeof(lsmtree_parameter));
		lsmtree_print_param(*target_param);
	}
	LSM=lsmtree_init(*target_param, sm);
	memset(all_set_data, -1, PAGESIZE);
	return 1;
}

void destroy_temp(lower_info *, struct algorithm *){
	free(target_param);
	lsmtree_print_log(LSM);
	lsmtree_free(LSM);
}

uint32_t remove_temp(request *const req){
	req->end_req(req);
	return 1;
}

uint32_t print_log_temp(){
	lsmtree_print_log(LSM);
	g_li->print_traffic(g_li);
	memset(LSM->monitor.compaction_cnt, 0, sizeof(uint32_t)*10);
	memset(LSM->monitor.compaction_input_entry_num, 0, sizeof(uint64_t)*10);
	memset(LSM->monitor.compaction_output_entry_num, 0, sizeof(uint64_t)*10);
	LSM->monitor.force_compaction_cnt=0;
	LSM->monitor.reinsert_cnt=0;
	//memset(&LSM->monitor, 0, sizeof(lsmtree_monitor));
	return 1;
}
bool running_flag=false;
uint32_t write_temp(request *const req){
	if(lsmtree_insert(LSM, req)==0){
		req->end_req(req);
	}
	else{

	}
	/*
	static bool print_test=false;
	if(print_test==false && g_li->req_type_cnt[DATAW]>=19755371){
		running_flag=true;
		g_li->print_traffic(g_li);
	}*/
	return 1;
}

uint32_t read_temp(request *const req){
	uint32_t res;
	res=lsmtree_read(LSM, req);
	return res;
}

uint32_t algo_dump(FILE *fp){
	return lsmtree_dump(LSM, fp);
}

uint32_t algo_load(lower_info *li, blockmanager *bm, struct algorithm *al, FILE *fp){
	LSM=lsmtree_load(fp, bm);
	g_li=li;
	return 1;
}

/***************************************test_function***************************************************/

bool __temp_end_req(request *req){
	inf_free_valueset(req->value, req->type==FS_GET_T?FS_MALLOC_R: FS_MALLOC_W);
	free(req);
	return true;
}

request *__make_dummy_request(uint32_t key, bool isread){
	request *res=(request*)calloc(1, sizeof(request));
	res->key=key;
	res->type=isread?FS_GET_T:FS_SET_T;
	res->value=inf_get_valueset(NULL,isread?FS_MALLOC_R:FS_MALLOC_W, PAGESIZE);
	res->end_req=__temp_end_req;
	return res;
}

static uint32_t *blending_array(uint32_t start_lba, uint32_t max_num){
	uint32_t *arr=(uint32_t*)calloc(max_num, sizeof(uint32_t));
	for(uint32_t i=0; i<max_num; i++){
		arr[i]=start_lba+i;
	}
	printf("%u~%u\n", arr[0], arr[max_num-1]);
	for(uint32_t i=0; i<max_num * 2; i++){
		uint32_t temp_idx1=rand()%max_num;
		uint32_t temp_idx2=rand()%max_num;
		uint32_t temp=arr[temp_idx1];
		arr[temp_idx1]=arr[temp_idx2];
		arr[temp_idx2]=temp;
	}
	return arr;
}

uint32_t test_function2(){
	uint32_t target_entry_num=10000;
	run *a=__lsm_populate_new_run(LSM, GUARD_BF, RUN_LOG, target_entry_num, 0);
	for(uint32_t i=0; i<target_entry_num; i++){
		request *req=__make_dummy_request(i,false);
		run_insert(a, req->key,UINT32_MAX, req->value->value, DATAW, LSM->shortcut, NULL);
		req->end_req(req);
	}
	run_insert_done(a, false);

	for(uint32_t i=0; i<target_entry_num; i++){
		request *req=__make_dummy_request(i, true);
		lsmtree_read(LSM, req);
	}
	return 1;
}

uint32_t test_function(){ //random+sequential+normal 
	uint32_t target_entry_num=_PPB*L2PGAP*4+_PPB*L2PGAP/2;
	run *a=__lsm_populate_new_run(LSM, TREE_MAP, RUN_LOG, target_entry_num, 0);
	run *b=__lsm_populate_new_run(LSM, TREE_MAP, RUN_LOG, target_entry_num, 0);

	uint32_t total_entry_num=_PPB*L2PGAP*9;
	uint32_t first_set_num=_PPB*L2PGAP*4;
	uint32_t second_set_num=_PPB*L2PGAP*3;
	
	uint32_t *first_set=blending_array(0, first_set_num);
	//0~first_setnum-1	
	uint32_t target_lba=first_set_num+_PPB*L2PGAP*2;
	uint32_t *second_set=blending_array(target_lba, second_set_num);

	uint32_t first_set_idx=0;
	for(uint32_t i=0; i<2; i++){
		for(uint32_t j=0; j<first_set_num/2; j++){
			request *req=__make_dummy_request(first_set[first_set_idx++],false);
			if(i==0){
				run_insert(a, req->key, UINT32_MAX, req->value->value, DATAW, LSM->shortcut, NULL);
			}
			else{
				run_insert(b, req->key, UINT32_MAX, req->value->value, DATAW, LSM->shortcut, NULL);
			}
			req->end_req(req);
		}
	}

	for(uint32_t i=first_set_num; i<first_set_num+_PPB*L2PGAP; i++){
		request *req=__make_dummy_request(i,false);
		run_insert(a, req->key, UINT32_MAX, req->value->value, DATAW, LSM->shortcut, NULL);
		req->end_req(req);
	}

	for(uint32_t i=first_set_num+_PPB*L2PGAP; i<first_set_num+_PPB*L2PGAP*2; i++){
		request *req=__make_dummy_request(i,false);
		run_insert(b, req->key, UINT32_MAX, req->value->value, DATAW, LSM->shortcut, NULL);
		req->end_req(req);
	}

	uint32_t second_set_idx=0;
	for(uint32_t i=0; i<2; i++){
		for(uint32_t j=0; j<second_set_num/2; j++){
			request *req=__make_dummy_request(second_set[second_set_idx++],false);
			if(i==0){
				run_insert(a, req->key, UINT32_MAX, req->value->value, DATAW, LSM->shortcut ,NULL);
			}
			else{
				run_insert(b, req->key, UINT32_MAX, req->value->value, DATAW, LSM->shortcut, NULL);
			}
			req->end_req(req);
		}
	}
	//printf("max insert lba:%u\n", temp_lba-1);

	run_insert_done(a, false);
	run_insert_done(b, false);

	run *merge_set[2];
	merge_set[0]=a;
	merge_set[1]=b;
	run *target=__lsm_populate_new_run(LSM, EXACT, RUN_NORMAL, target_entry_num*2, 1);
	run_merge(2,merge_set, target, false, LSM);


	for(uint32_t i=0; i<total_entry_num; i++){
		request *req=__make_dummy_request(i, true);
		lsmtree_read(LSM, req);
	}

	free(first_set);
	free(second_set);
	return 1;
}
