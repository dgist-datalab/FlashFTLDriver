#include "level.h"
#include "lsmtree.h"
#include "page_manager.h"
#include "io.h"
#include <stdint.h>
#include <set>
extern lsmtree LSM;
static char dummy[8192];
static inline std::set<uint32_t> *get_lba_set(uint32_t from, uint32_t to, 
		uint32_t num_of_set, bool sequntial);
static char *flush_set(std::set<uint32_t> *target, bool map_write_same_block, uint32_t *map_ppa);

sst_file *get_dummy_block_sst(std::set<uint32_t> **target, uint32_t num, 
		uint32_t total_lba_num){
	uint32_t remain_page=page_manager_get_remain_page(LSM.pm, false);
	if(total_lba_num+num > remain_page*2){
		page_manager_move_next_seg(LSM.pm, false, false, DATASEG);
	}

	sst_file *res=sst_init_empty(BLOCK_FILE);
	map_range *mr_set=(map_range*)malloc(sizeof(map_range)*num);
	uint32_t map_ppa;
	uint32_t prev_ppa=UINT32_MAX;
	uint32_t start_piece_ppa;
	for(uint32_t i=0; i<num; i++){
		uint32_t start_lba, end_lba;
		char *map_data=flush_set(target[i], true, &map_ppa);
		key_ptr_pair* kp_set=(key_ptr_pair*)map_data;
		if(i==0){	
			start_piece_ppa=kp_set[0].piece_ppa;
		}
		start_lba=kp_set[0].lba;
		end_lba=kp_get_end_lba(map_data);
		mr_set[i].start_lba=start_lba;
		mr_set[i].end_lba=end_lba;
		mr_set[i].ppa=map_ppa;
		validate_map_ppa(LSM.pm->bm, map_ppa, start_lba, end_lba, true);

		if(prev_ppa==UINT32_MAX){
			prev_ppa=map_ppa;
		}
		else if(prev_ppa/_PPS!=map_ppa/_PPS){
			EPRINT("sst must be placed same block", true);
		}
		free(map_data);
	}
	
	sst_set_file_map(res, num, mr_set);
	res->file_addr.piece_ppa=start_piece_ppa;
	res->end_ppa=map_ppa;
	res->map_num=num;
	res->start_lba=mr_set[0].start_lba;
	res->end_lba=mr_set[num-1].end_lba;
	res->_read_helper=NULL;
	return res;
}

sst_file *get_dummy_page_sst(std::set<uint32_t> *target){
	char *map_data=flush_set(target, false, NULL);
	key_ptr_pair* kp_set=(key_ptr_pair*)map_data;
	uint32_t map_ppa=page_manager_get_new_ppa(LSM.pm, true, MAPSEG);
	uint32_t start_lba=kp_set[0].lba;
	uint32_t end_lba=kp_get_end_lba(map_data);
	validate_map_ppa(LSM.pm->bm, map_ppa, start_lba, end_lba, true);
	io_manager_test_write(map_ppa, map_data, MAPPINGW);

	sst_file *res=sst_init_empty(PAGE_FILE);
	res->start_lba=start_lba;
	res->end_lba=end_lba;
	res->_read_helper=NULL;
	res->file_addr.map_ppa=map_ppa;

	free(map_data);
	return res;
}

level *making_leveling(uint32_t sst_num, uint32_t populate_sst_num, bool issequential, uint32_t idx, bool wisckey){
	level *res=level_init(sst_num, 1, wisckey? LEVELING_WISCKEY:LEVELING, idx);
	std::set<uint32_t>** lba_set=new std::set<uint32_t>*[sst_num];
	if(wisckey){
		for(uint32_t i=0; i<populate_sst_num; i++){
			lba_set[i]=get_lba_set(0, RANGE, KP_IN_PAGE, issequential);
			sst_file *sptr=get_dummy_page_sst(lba_set[i]);
			level_append_sstfile(res, sptr, true);
			sst_free(sptr, LSM.pm);
		}
	}
	else{
		for(uint32_t i=0; i<populate_sst_num; i++){
			lba_set[i]=get_lba_set(0, RANGE, KP_IN_PAGE, issequential);
		}
		uint32_t total_lba=KP_IN_PAGE * populate_sst_num;
		uint32_t round=(total_lba+populate_sst_num)/_PPS+((total_lba+populate_sst_num)%_PPS?1:0);
		uint32_t sum_prev_sst=0;
		for(uint32_t i=0; i<round; i++){
			uint32_t remain_sst_num=(populate_sst_num-sum_prev_sst);
			uint32_t now_sst_num=(remain_sst_num*KP_IN_PAGE+remain_sst_num)>_PPS?
				_PPS/(KP_IN_PAGE+1):
				remain_sst_num;
			sst_file *sptr=get_dummy_block_sst(&lba_set[sum_prev_sst], now_sst_num, now_sst_num*KP_IN_PAGE);
			level_append_sstfile(res, sptr, true);
			sst_free(sptr, LSM.pm);
			sum_prev_sst+=now_sst_num;
		}
	}
	delete lba_set;
	return res;
}

level *making_tiering(uint32_t run_num, uint32_t sst_num, uint32_t populate_run_num, 
	bool issequential, uint32_t idx){
	level *res=level_init(sst_num, run_num, TIERING, idx);
	std::set<uint32_t>** lba_set=new std::set<uint32_t>*[sst_num];
	for(uint32_t i=0; i<populate_run_num*(sst_num/run_num); i++){
		lba_set[i]=get_lba_set(0, RANGE, KP_IN_PAGE, issequential);
	}
	uint32_t sst_cnt_consume=0;
	for(uint32_t i=0; i<populate_run_num; i++){
		uint32_t run_per_sst_num=sst_num/run_num;
		run *new_run=run_init(run_per_sst_num, UINT32_MAX, 0);

		uint32_t total_lba=KP_IN_PAGE * run_per_sst_num;
		uint32_t round=(total_lba+run_per_sst_num)/_PPS+((total_lba+run_per_sst_num)%_PPS?1:0);
		uint32_t sum_prev_sst=0;
		for(uint32_t i=0; i<round; i++){
			uint32_t remain_sst_num=(run_per_sst_num-sum_prev_sst);
			uint32_t now_sst_num=(remain_sst_num*KP_IN_PAGE+remain_sst_num)>_PPS?
				_PPS/(KP_IN_PAGE+1):
				remain_sst_num;
			sst_file *sptr=get_dummy_block_sst(&lba_set[sst_cnt_consume+sum_prev_sst], 
					now_sst_num, now_sst_num*KP_IN_PAGE);

			run_append_sstfile_move_originality(new_run, sptr);
			sst_free(sptr, LSM.pm);
			sum_prev_sst+=now_sst_num;
		}
		sst_cnt_consume+=sum_prev_sst;
		level_update_run_at_move_originality(res, i, new_run, true);
		run_free(new_run);
	}
	delete lba_set;
	return res;
}

uint32_t lsmtree_testing(){
	level *temp_disk[2]=NULL;
	/*small issequential test*/
	{
		/*LW2LE*/
		{
			level *lw=making_leveling(10, 9, true, 0, true);
			level *le=making_leveling(100, 1, true, 1, false);
			level_free(compaction_LW2LE(LSM.cm, lw, le), LSM.pm);
			level_free(lw, LSM.pm);
			level_free(le, LSM.pm);
		}

		/*TI2TI*/
		{
			level *ti1=making_tiering(10, 10, 9, true, 0);
			level *ti2=making_tiering(10, 100, 1, true, 1);
			level_free(compaction_TI2TI(LSM.cm, ti1, ti2), LSM.pm);	
			level_free(ti1, LSM.pm);
			level_free(ti2, LSM.pm);
		}
	}
	/*small random test*/
	{
		/*LW2LE*/
		{
			level *lw=making_leveling(10, 9, false, 0, true);
			level *le=making_leveling(100, 1, false, 1, false);
			level_free(compaction_LW2LE(LSM.cm, lw, le), LSM.pm);
			level_free(lw, LSM.pm);
			level_free(le, LSM.pm);
		}

		/*TI2TI*/
		{
			level *ti1=making_tiering(10, 10, 9, false, 0);
			level *ti2=making_tiering(10, 100, 1, false, 1);
			level_free(compaction_TI2TI(LSM.cm, ti1, ti2), LSM.pm);
			level_free(ti1, LSM.pm);
			level_free(ti2, LSM.pm);
		}

	}
	/*big sequntial test*/
	{
		/*LW2LE*/
		{
			level *lw=making_leveling(10, 9, true, 0, true);
			level *le=making_leveling(100, 80, true, 1, false);
			level_free(compaction_LW2LE(LSM.cm, lw, le), LSM.pm);
			level_free(lw, LSM.pm);
			level_free(le, LSM.pm);
		}

		/*TI2TI*/
		{
			level *ti1=making_tiering(10, 10, 9, true, 0);
			level *ti2=making_tiering(10, 100, 8, true, 1);
			level_free(compaction_TI2TI(LSM.cm, ti1, ti2), LSM.pm);
			level_free(ti1, LSM.pm);
			level_free(ti2, LSM.pm);
		}
	}
	/*big random test*/
	{
		/*LW2LE*/
		{
			level *lw=making_leveling(10, 9, false, 0, true);
			level *le=making_leveling(100, 80, false, 1, false);
			level_free(compaction_LW2LE(LSM.cm, lw, le), LSM.pm);
			level_free(lw, LSM.pm);
			level_free(le, LSM.pm);
		}

		/*TI2TI*/
		{
			level *ti1=making_tiering(10, 10, 9, false, 0);
			level *ti2=making_tiering(10, 100, 8, false, 1);
			level_free(compaction_TI2TI(LSM.cm, ti1, ti2), LSM.pm);
			level_free(ti1, LSM.pm);
			level_free(ti2, LSM.pm);
		}
	}
	return 1;
}

/*from this line, utility code*/
static inline std::set<uint32_t> *get_lba_set(uint32_t from, uint32_t to, 
		uint32_t num_of_set, bool issequential){
	std::set<uint32_t> * res=new std::set<uint32_t>();
	while(res->size()!=num_of_set){
		res->insert(issequential?from++:rand()%(to-from+1)+from);
	}
	return res;
}

static char *flush_set(std::set<uint32_t> *target, bool map_write_same_block, uint32_t *map_ppa){
	std::set<uint32_t>::iterator iter;
	char *key_ptr=(char*)malloc(PAGESIZE);
	char *oob;
	memset(key_ptr, -1, PAGESIZE);
	key_ptr_pair *mapping=(key_ptr_pair*)key_ptr;
	uint32_t inter_idx=0;
	uint32_t cnt=0;
	uint32_t ppa;
	uint32_t remain_page=page_manager_get_remain_page(LSM.pm, false);
	if(map_write_same_block && target->size()+(map_write_same_block?1:0) > remain_page*L2PGAP){
		page_manager_move_next_seg(LSM.pm, false, false, DATASEG);
	}

	for(; iter!=target->end(); iter++){
		inter_idx=cnt%L2PGAP;
		if(inter_idx==0){
			ppa=page_manager_get_new_ppa(LSM.pm, 
					false, map_write_same_block?DATASEG:SEPDATASEG);
			oob=LSM.pm->bm->get_oob(LSM.pm->bm, ppa);
		}
		mapping[cnt].lba=*iter;
		mapping[cnt].piece_ppa=ppa*L2PGAP+inter_idx;
		validate_piece_ppa(LSM.pm->bm, 1, &mapping[cnt].piece_ppa, 
				&mapping[cnt].lba, true);
		if(inter_idx==(L2PGAP-1)){
			io_manager_test_write(ppa, dummy, DATAW);
		}
		cnt++;
	}

	if(map_write_same_block){
		ppa=page_manager_get_new_ppa(LSM.pm, false, DATASEG);
		*map_ppa=ppa;
		if(ppa/_PPS!=mapping[cnt-1].piece_ppa/2/_PPS){
			EPRINT("faile to assign ppa for block sst", true);
		}
		io_manager_test_write(ppa, key_ptr, DATAW);
	}
	delete target;
	return key_ptr;
}
