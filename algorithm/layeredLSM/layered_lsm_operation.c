#include "block_table.h"
#include "sorted_table.h"
#include "page_aligner.h"
#include "summary_page.h"
#include "../../include/container.h"
#include "../../include/debug_utils.h"
#include "./mapping_function.h"

uint32_t argument_set_temp(int ,char **){return 1;}
uint32_t operation_temp(request *const ){return 1;}
uint32_t print_log_temp(){return 1;}

uint32_t create_temp(lower_info *,blockmanager *, struct algorithm *);
void destroy_temp(lower_info *, struct algorithm *);
uint32_t write_temp(request *const );
uint32_t read_temp(request *const );
uint32_t test_function();

struct algorithm layered_lsm={
	.argument_set=argument_set_temp,
	.create=create_temp,
	.destroy=destroy_temp,
	.read=read_temp,
	.write=write_temp,
	.flush=operation_temp,
	.remove=operation_temp,
	.test=test_function,
	.print_log=print_log_temp,
};

pp_buffer *page_aligner;
L2P_bm *bm;
st_array *sa;
summary_page *sp;
lower_info* g_li;
mapping_function *e_mf;
mapping_function **b_mf;

uint32_t create_temp(lower_info *li,blockmanager *sm, struct algorithm *){
	page_aligner=pp_init();
	bm=L2PBm_init(sm);
	sa=st_array_init(RANGE, bm);
	sp=sp_init();
	g_li=li;
	e_mf=map_function_factory(EXACT, RANGE, 1);

	b_mf=(mapping_function**)malloc(sizeof(mapping_function*)*((RANGE/100)+1));
	for(uint32_t i=0; i<((RANGE/100)+1); i++){
		b_mf[i]=map_function_factory(BF, 100, 0.5);
	}
	return 1;
}

void destroy_temp(lower_info *, struct algorithm *){
	pp_free(page_aligner);
	L2PBm_free(bm);
	e_mf->free(e_mf);

	for(uint32_t i=0; i<((RANGE/100)+1); i++){
		b_mf[i]->free(b_mf[i]);
	}
	free(b_mf);
}

static inline void *temp_end_req(algo_req *req){
	request *p_req=(request *)req->param;
	if(req->type==DATAW){
		inf_free_valueset(req->value, FS_MALLOC_W);
	}
	else if (req->type==DATAR && req->ppa%L2PGAP!=0){
		memmove(&req->value->value[0], &req->value->value[(req->ppa%L2PGAP)*LPAGESIZE], LPAGESIZE);
	}

	if(p_req){
		p_req->end_req(p_req);
	}
	free(req);
	return NULL;
}

static inline algo_req *temp_allocate_algo_req(uint32_t type, uint32_t ppa, value_set *value, void *param){
	algo_req *res=(algo_req*)calloc(1, sizeof(algo_req));
	res->type=type;
	res->ppa=ppa;
	res->param=param;
	res->value=value;
	res->end_req=temp_end_req;
	/*
	switch(type){
		case DATAR:
			break;
		case DATAW:
			break;
	}
	*/
	return res;
}

uint32_t write_temp(request *const req){
	static bool debug_flag=false;
	if(pp_insert_value(page_aligner, req->key, req->value->value)){
		uint32_t psa;
		uint32_t target_ppa;
		static int bf_test_cnt=0;
	//	DEBUG_CNT_PRINT(write_temp, 9272, __FUNCTION__, __LINE__);
		for(uint32_t i=0; i<L2PGAP; i++){
			psa=st_array_write_translation(sa);
			if(i==0){
				target_ppa=psa/L2PGAP;
			}
			sp_insert(sp, page_aligner->LBA[i], psa);
			e_mf->insert(e_mf, page_aligner->LBA[i], psa);
			b_mf[bf_test_cnt/100]->insert(b_mf[bf_test_cnt++/100], page_aligner->LBA[i], psa);
		}

		bm->segment_manager->set_oob(bm->segment_manager, (char*)page_aligner->LBA, sizeof(uint32_t) * L2PGAP, target_ppa);
		algo_req *areq=temp_allocate_algo_req(DATAW, target_ppa, pp_get_write_target(page_aligner, false), NULL);
		g_li->write(target_ppa, PAGESIZE, areq->value, ASYNC, areq);

		if(sa->summary_write_alert){
			debug_flag=true;
			psa=st_array_summary_translation(sa);
			areq=temp_allocate_algo_req(DATAW, target_ppa, inf_get_valueset(sp_get_data(sp), 
						FS_MALLOC_W, PAGESIZE), NULL);
			g_li->write(psa/L2PGAP, PAGESIZE, areq->value, ASYNC, areq);
			sp_reinit(sp);
		}
		pp_reinit_buffer(page_aligner);
	}
	req->end_req(req);
	return 1;
}

uint32_t read_temp(request *const req){
	char *res=pp_find_value(page_aligner, req->key);
	if(res){
		memcpy(req->value->value, res, LPAGESIZE);
		req->end_req(req);
	}
	else{
		uint32_t psa=mf->query(mf, req->key);
		if(psa!=st_array_read_translation(sa, req->key)){
			EPRINT("error", true);
		}
		algo_req *areq=temp_allocate_algo_req(DATAR, psa, req->value, req);
		g_li->read(psa/L2PGAP, PAGESIZE, req->value, ASYNC, areq);
	}
	return 1;
}

uint32_t test_function(){
	return 1;
}
