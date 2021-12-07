#include "summary_page.h"
#include "../../include/debug_utils.h"
#include "../../include/search_template.h"
#include "../../interface/interface.h"
extern char all_set_data[PAGESIZE];
summary_page *sp_init(){
	summary_page *res=(summary_page *)malloc(sizeof(summary_page));
	res->write_pointer=0;
	res->value=inf_get_valueset(all_set_data, FS_MALLOC_W, PAGESIZE);
	res->body=res->value->value;
	return res;
}

void sp_free(summary_page *sp){
	inf_free_valueset(sp->value, FS_MALLOC_W);
	free(sp);
}

void sp_reinit(summary_page *sp){
	memset(sp->body, -1, PAGESIZE);
	sp->write_pointer=0;
}

bool sp_insert(summary_page *sp, uint32_t lba, uint32_t psa){
	summary_pair* p=(summary_pair*)(&sp->body[sp->write_pointer*sizeof(summary_pair)]);
	p->lba=lba;
	p->psa=psa;
	sp->write_pointer++;
	if(sp->write_pointer==MAX_CUR_POINTER) return true;
	else return false;
}

bool sp_insert_spair(summary_page *sp, summary_pair p){
	memcpy(&sp->body[sp->write_pointer*sizeof(summary_pair)], &p, sizeof(summary_pair));
	sp->write_pointer++;
	if(sp->write_pointer==MAX_CUR_POINTER) return true;
	else return false;
}

value_set *sp_get_data(summary_page *sp){
	return sp->value;
}

int summary_pair_cmp(summary_pair p, uint32_t target){return p.lba-target;}
uint32_t sp_find_psa(summary_page *sp, uint32_t lba){
	uint32_t res=0;
	bs_search((summary_pair*)sp->body, 0, MAX_IDX_SP, lba, summary_pair_cmp, res);
	return res;
}

void sp_print_all(summary_page *sp){
	summary_pair p;
	uint32_t idx;
	for_each_sp_pair(sp, idx, p){
		printf("%u %u->%u\n", idx, p.lba, p.psa);
	}
}

summary_page_iter* spi_init(summary_page *sp){
	summary_page_iter *res=(summary_page_iter*)malloc(sizeof(summary_page_iter));
	res->sp=sp;
	res->body=sp->body;
	res->read_pointer=0;
	return res;
}

summary_page_iter* spi_init_by_data(char *data){
	summary_page_iter *res=(summary_page_iter*)malloc(sizeof(summary_page_iter));
	res->body=data;
	res->sp=NULL;
	res->read_pointer=0;
	return res;
}

summary_pair spi_pick_pair(summary_page_iter *spi){
	static summary_pair end_of_pair={UINT32_MAX, UINT32_MAX};
	if(spi->read_pointer==MAX_CUR_POINTER){
		return end_of_pair;
	}
	else{
		return (((summary_pair*)spi->body)[spi->read_pointer * sizeof(summary_pair)]);
	}
}

void spi_move_forward(summary_page_iter* spi){
	spi->read_pointer++;
}

void spi_move_backward(summary_page_iter* spi){
	if(spi->read_pointer==0){
		EPRINT("cannot decrease read pointer",true);
	}
	spi->read_pointer--;
}

void spi_free(summary_page_iter* spi){
	free(spi);
}
