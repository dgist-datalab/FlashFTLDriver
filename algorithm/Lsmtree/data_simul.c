#include "lsmtree.h"
#include "../../include/lsm_settings.h"
#include "../../include/utils/kvssd.h"
#include "../../include/data_struct/redblack.h"
extern lsmtree LSM;
#ifdef EMULATOR
bool tester;
uint32_t lsm_simul_put(ppa_t ppa, KEYT key){
	KEYT* t_key=(KEYT*)malloc(sizeof(KEYT));
	kvssd_cpy_key(t_key,&key);
	rb_insert_int(LSM.rb_ppa_key,ppa,t_key);
	return 1;
}

KEYT* lsm_simul_get(ppa_t ppa){
	Redblack res;
	rb_find_int(LSM.rb_ppa_key,ppa,&res);
	return (KEYT*)res->item;
}

void lsm_simul_del(ppa_t ppa){
	Redblack res;
	rb_find_int(LSM.rb_ppa_key,ppa,&res);
	rb_delete(res,true);
}
#endif
