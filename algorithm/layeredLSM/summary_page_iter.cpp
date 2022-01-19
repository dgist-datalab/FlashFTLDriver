#include "summary_page.h"
#include "../../include/debug_utils.h"
extern lower_info *g_li;
extern bool debug_flag;

static void *__spi_read_end_req(algo_req *req){
	summary_page_iter *spi=(summary_page_iter*)req->param;
	if(req->type!=MAPPINGR){
		EPRINT("not_allowed type", true);
	}
	spi->read_flag=true;
	fdriver_unlock(&spi->read_done);
	free(req);
	return NULL;
}

static void __spi_issue_read(summary_page_iter *spi){
	algo_req *res=(algo_req*) calloc(1, sizeof(algo_req));
	res->ppa=spi->spm->ppa;
	res->param=(void *)spi;
	res->type=MAPPINGR;
	res->end_req=__spi_read_end_req;
	fdriver_mutex_init(&spi->read_done);
	fdriver_lock(&spi->read_done);
	spi->read_flag=false;
	spi->lock_deallocate=false;
	g_li->read(res->ppa, PAGESIZE, spi->value, res);
}

summary_page_iter* spi_init(summary_page_meta *spm){
	summary_page_iter *res=(summary_page_iter*)malloc(sizeof(summary_page_iter));
	res->spm=spm;
	res->read_pointer=0;
	
	res->value=inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);
	res->body=res->value->value;

	spm->private_data=(void*)res;
	spm->pr_type=READ_PR;

	res->iter_done_flag=false;

	__spi_issue_read(res);
	return res;
}



summary_page_iter* spi_init_by_data(char *data){
	summary_page_iter *res=(summary_page_iter*)malloc(sizeof(summary_page_iter));
	res->body=data;
	res->spm=NULL;
	res->read_pointer=0;
	return res;
}

summary_pair spi_pick_pair(summary_page_iter *spi){
	if(!spi->read_flag){
		fdriver_lock(&spi->read_done);
	}
	if(spi->lock_deallocate==false){
		fdriver_destroy(&spi->read_done);
		spi->lock_deallocate=true;
	}
	static summary_pair end_of_pair={UINT32_MAX, UINT32_MAX};
	if(spi->read_pointer==NORMAL_CUR_END_PTR){
		spi->iter_done_flag=true;
		return end_of_pair;
	}
	else{
		summary_pair res=(((summary_pair*)spi->body)[spi->read_pointer]);
		if(res.lba==UINT32_MAX){
			spi->iter_done_flag=true;
		}
		return res; 	
	}
}
bool spi_move_forward(summary_page_iter* spi){
	spi->read_pointer++;
	if(spi->read_pointer==NORMAL_CUR_END_PTR){
		spi->iter_done_flag=true;
		inf_free_valueset(spi->value, FS_MALLOC_R);
		spi->value=NULL;
		return true;
	}
	if(spi_pick_pair(spi).lba==UINT32_MAX){
		spi->iter_done_flag=true;
		inf_free_valueset(spi->value, FS_MALLOC_R);
		spi->value=NULL;
		return true;
	}
	return false;
}

void spi_move_backward(summary_page_iter* spi){
	if(spi->read_pointer==0){
		EPRINT("cannot decrease read pointer",true);
	}
	spi->read_pointer--;
}

void spi_free(summary_page_iter* spi){
	if(spi->value){
		inf_free_valueset(spi->value, FS_MALLOC_R);
	}
	spi->spm->pr_type=NO_PR;
	spi->spm->private_data=NULL;
	free(spi);
}
