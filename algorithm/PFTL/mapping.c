#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include "mapping.h"
#include "gc.h"
#include "utils.h"

extern algorithm pftl;

void pftl_mapping_create() {
	pm_body *p;

	//mapping initiate
	p = (pm_body *)calloc(sizeof(pm_body), 1);
	p->mapping = (uint32_t *)malloc(sizeof(uint32_t) * _NOP * L2PGAP);
	for(int i = 0; i < _NOP * L2PGAP; i++) {
		p->mapping[i] = UINT_MAX;
	}
	
	//reserve seg is for GC
	p->reserve = pftl.bm->get_segment(pftl.bm, true);
	p->active = pftl.bm->get_segment(pftl.bm, false);

	pftl.algo_body = (void *)p;

	return ;
}

void pftl_mapping_free() {
	pm_body *p;

	p = (pm_body *)pftl.algo_body;
	free(p->mapping);
	free(p);

	return ;
}

uint32_t pftl_assign_ppa(KEYT *lba) {
	uint32_t res;
	pm_body *p;
	KEYT s_lba;

	res = pftl_bm_assign_ppa();
	p = (pm_body *)pftl.algo_body;

	for(uint32_t i = 0; i < L2PGAP; i++) {
		s_lba = lba[i];

		if(p->mapping[s_lba] != UINT_MAX) {
			pftl_invalidate_ppa(p->mapping[s_lba]);
		}

		p->mapping[s_lba] = res * L2PGAP + i;
        pftl_validate_ppa(p->mapping[s_lba]);
	}
    
	pftl.bm->set_oob(pftl.bm, (char *)lba, sizeof(KEYT) * L2PGAP, res);

	return res;
}

uint32_t pftl_get_mapped_ppa(uint32_t lba) {
	uint32_t res;
	pm_body *p;

	p = (pm_body *)pftl.algo_body;
	res = p->mapping[lba];

	return res;
}


uint32_t pftl_map_trim(uint32_t lba) {
	uint32_t res;
	pm_body *p;

	p = (pm_body *)pftl.algo_body;
	res=p->mapping[lba];

	if(res == UINT32_MAX) {
		return 0;
	}
	else {
		pftl_invalidate_ppa(res);
		p->mapping[lba] = UINT32_MAX;

		return 1;
	}
}

uint32_t pftl_gc_update_mapping(KEYT *lba, uint32_t idx) {
	pm_body *p;
	KEYT s_lba;
	uint32_t res;

	p = (pm_body *)pftl.algo_body;
	res = pftl.bm->get_page_num(pftl.bm, p->reserve);
	
	for(uint32_t i = 0; i < idx; i++) {
		s_lba = lba[i];
		p->mapping[s_lba] = res * L2PGAP + i;
        pftl_validate_ppa(p->mapping[s_lba]);
	}
	
	pftl.bm->set_oob(pftl.bm, (char *)lba, sizeof(KEYT) * L2PGAP, res);

	return res;
}
