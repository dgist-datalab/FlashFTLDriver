#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "utils.h"
#include "mapping.h"
#include "gc.h"

extern algorithm pftl;

void pftl_validate_ppa(uint32_t log_ppa) {
    if (24536 == log_ppa || 24537 == log_ppa || 1933569 == log_ppa) {
        printf("%d validate!\n",log_ppa);
    }

    pftl.bm->populate_bit(pftl.bm, log_ppa);
}

void pftl_invalidate_ppa(uint32_t log_ppa) {
	if(log_ppa < 32768) {
		//abort();
	}

	if(log_ppa == 24537 || log_ppa == 1933569) {
		printf("%d invalidate!\n", log_ppa);
	}

	pftl.bm->unpopulate_bit(pftl.bm, log_ppa);
}

ppa_t pftl_bm_assign_ppa() {
	uint32_t res;
	pm_body *p;
    
    p = (pm_body *)pftl.algo_body;
	if(pftl.bm->check_full(pftl.bm, p->active, MASTER_PAGE) && pftl.bm->is_gc_needed(pftl.bm)) {
		pftl_gc();
	}

    while((res = pftl.bm->get_page_num(pftl.bm, p->active)) == UINT32_MAX) {
		pftl.bm->free_segment(pftl.bm, p->active);
		p->active = pftl.bm->get_segment(pftl.bm, false);
    }
	
    return res;
}