#include "level.h"
#include "lsmtree.h"
#include "page_manager.h"
#include "io.h"
#include "function_test.h"
#include <stdint.h>
#include <set>
extern lsmtree LSM;
char *dummy_data;
static inline std::set<uint32_t> *get_lba_set(uint32_t from, uint32_t to, 
		uint32_t num_of_set, bool sequntial);
static inline std::set<uint32_t> *split_lba_set(std::set<uint32_t> *from, uint32_t num_of_set);
static char *flush_set(std::set<uint32_t> *target, bool map_write_same_block, 
		uint32_t *map_ppa, uint32_t version_idx);

sst_file *get_dummy_block_sst(std::set<uint32_t> **target, uint32_t num, 
		uint32_t total_lba_num, uint32_t version_idx){
	uint32_t remain_page=page_manager_get_remain_page(LSM.pm, false);
	if(total_lba_num/2+num > remain_page){
		page_manager_move_next_seg(LSM.pm, false, false, DATASEG);
	}

	sst_file *res=sst_init_empty(BLOCK_FILE);
	map_range *mr_set=(map_range*)malloc(sizeof(map_range)*num);
	uint32_t map_ppa;
	uint32_t prev_ppa=UINT32_MAX;
	uint32_t start_piece_ppa;
	for(uint32_t i=0; i<num; i++){
		uint32_t start_lba, end_lba;
		char *map_data=flush_set(target[i], true, &map_ppa, version_idx);
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
			printf("PPS:%u\n",_PPS);
			EPRINT("sst must be placed same block", true);
		}
		else{
			prev_ppa=map_ppa;
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

sst_file *get_dummy_page_sst(std::set<uint32_t> *target, uint32_t version_idx){
	char *map_data=flush_set(target, false, NULL, version_idx);
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

level *making_leveling(uint32_t sst_num, uint32_t populate_sst_num, bool issequential, 
		uint32_t idx, bool wisckey){
	level *res=level_init(sst_num, 1, wisckey? LEVELING_WISCKEY:LEVELING, idx);
	std::set<uint32_t> *sorted_set=NULL;
	std::set<uint32_t>* lba_set[100];
	uint32_t version_idx;
	if(populate_sst_num){
		version_idx=version_get_empty_ridx(LSM.last_run_version, idx);
		version_populate_run(LSM.last_run_version, version_idx, idx);
		sorted_set=get_lba_set(0, RANGE, KP_IN_PAGE * populate_sst_num, issequential);
	}
	if(wisckey){
		for(uint32_t i=0; i<populate_sst_num; i++){
			lba_set[i]=split_lba_set(sorted_set, KP_IN_PAGE);
			sst_file *sptr=get_dummy_page_sst(lba_set[i], version_idx);
			level_append_sstfile(res, sptr, true);
			sst_free(sptr, LSM.pm);
		}
	}
	else{
		for(uint32_t i=0; i<populate_sst_num; i++){
			lba_set[i]=split_lba_set(sorted_set, KP_IN_PAGE);
		}
		uint32_t total_lba=KP_IN_PAGE * populate_sst_num;
		uint32_t round=(total_lba+populate_sst_num)/_PPS+((total_lba+populate_sst_num)%_PPS?1:0);
		uint32_t sum_prev_sst=0;
		for(uint32_t i=0; i<round; i++){
			uint32_t remain_sst_num=(populate_sst_num-sum_prev_sst);
			uint32_t now_sst_num=(remain_sst_num*KP_IN_PAGE+remain_sst_num)>_PPS?
				_PPS/(KP_IN_PAGE+1):
				remain_sst_num;
			sst_file *sptr=get_dummy_block_sst(&lba_set[sum_prev_sst], now_sst_num, now_sst_num*KP_IN_PAGE, version_idx);
			level_append_sstfile(res, sptr, true);
			sst_free(sptr, LSM.pm);
			sum_prev_sst+=now_sst_num;
		}
	}

	if(populate_sst_num){
		delete sorted_set;
	}
	return res;
}

level *making_tiering(uint32_t run_num, uint32_t sst_num, uint32_t populate_run_num, 
	bool issequential, uint32_t idx){
	level *res=level_init(sst_num, run_num, TIERING, idx);
	std::set<uint32_t> *run_set[100];
	std::set<uint32_t>* lba_set[100];
	for(uint32_t i=0; i<populate_run_num; i++){
		run_set[i]=get_lba_set(0, RANGE, (sst_num/run_num)*KP_IN_PAGE, issequential);
	}
	uint32_t sst_cnt_consume=0;
	for(uint32_t i=0; i<populate_run_num; i++){
		uint32_t run_per_sst_num=sst_num/run_num;
		run *new_run=run_init(run_per_sst_num, UINT32_MAX, 0);
		uint32_t version_idx=version_get_empty_ridx(LSM.last_run_version, idx);
		version_populate_run(LSM.last_run_version, version_idx, idx);

		uint32_t total_lba=KP_IN_PAGE * run_per_sst_num;
		uint32_t round=(total_lba+run_per_sst_num)/_PPS+((total_lba+run_per_sst_num)%_PPS?1:0);
		uint32_t sum_prev_sst=0;

		for(uint32_t j=0; j<round; j++){
			uint32_t remain_sst_num=(run_per_sst_num-sum_prev_sst);
			uint32_t now_sst_num=(remain_sst_num*KP_IN_PAGE+remain_sst_num)>_PPS?
				_PPS/(KP_IN_PAGE+1):
				remain_sst_num;

			for(uint32_t k=0; k<now_sst_num; k++){
				lba_set[sst_cnt_consume+sum_prev_sst+k]=split_lba_set(run_set[i], KP_IN_PAGE);
			}

			sst_file *sptr=get_dummy_block_sst(&lba_set[sst_cnt_consume+sum_prev_sst], 
					now_sst_num, now_sst_num*KP_IN_PAGE, version_idx);

			run_append_sstfile_move_originality(new_run, sptr);
			sst_free(sptr, LSM.pm);
			sum_prev_sst+=now_sst_num;
		}
		sst_cnt_consume+=sum_prev_sst;
		level_update_run_at_move_originality(res, i, new_run, true);
		run_free(new_run);
	}

	for(uint32_t i=0;i<populate_run_num; i++){
		delete run_set[i];
	}
	return res;
}

static inline void compaction_test(level **disk,
		level *(*comp_func)(compaction_master* cm, level *, level *)){
	level *temp=comp_func(LSM.cm, disk[0], disk[1]);
	level_consistency_check(temp);
	level_free(temp, LSM.pm);
	level_free(disk[0], LSM.pm);
	level_free(disk[1], LSM.pm);
}

uint32_t lsmtree_testing(){
	level *temp_disk[2];
	temp_disk[0]=level_init(10, 10, 1, 0);
	temp_disk[1]=level_init(10, 10, 1, 1);
	level *disk[2];
	dummy_data=(char*)malloc(PAGESIZE);
	LSM.param.normal_size_factor=10;
	LSM.function_test_flag=true;
	/*big sequential test*/
	{
		/*LE2LE
		{
			temp_disk[0]->level_type=LEVELING;
			temp_disk[1]->level_type=LEVELING;
			version *now_version=version_init(2, 2, RANGE, temp_disk, 2);
			version *temp_version=LSM.last_run_version;
			LSM.last_run_version=now_version;
			disk[0]=making_leveling(10, 9, true, 0, false);
			disk[1]=making_leveling(100, 80, true, 1, false);
			compaction_test(disk, compaction_LE2LE);
			LSM.last_run_version=temp_version;
			version_free(now_version);
			printf("big sequential test of LE2LE is passed\n");
		}*/
	}

	/*big random test*/
	{
		/*LE2LE*/
		{
			temp_disk[0]->level_type=LEVELING;
			temp_disk[1]->level_type=LEVELING;
			version *now_version=version_init(2, 2, RANGE, temp_disk, 2);
			version *temp_version=LSM.last_run_version;
			LSM.last_run_version=now_version;
			disk[0]=making_leveling(10, 9, false, 0, false);
			disk[1]=making_leveling(100, 90, false, 1, false);
			compaction_test(disk, compaction_LE2LE);
			LSM.last_run_version=temp_version;
			version_free(now_version);
			printf("big random test of LE2LE is passed\n");
		}

		/*LE2TI*/
		{
			temp_disk[0]->level_type=LEVELING;
			temp_disk[1]->level_type=TIERING;
			version *now_version=version_init(11, 10, RANGE, temp_disk, 2);
			version *temp_version=LSM.last_run_version;
			LSM.last_run_version=now_version;
			disk[0]=making_leveling(10, 9, false, 0, false);
			disk[1]=making_tiering(10, 100, 8, false, 1);

			compaction_test(disk, compaction_LE2TI);
			LSM.last_run_version=temp_version;
			version_free(now_version);
			printf("big random test of LE2TI is passed\n");
		}
	}
	free(dummy_data);
	exit(1);
	return 1;
}

/*from this line, utility code*/
static inline std::set<uint32_t> *get_lba_set(uint32_t from, uint32_t to, 
		uint32_t num_of_set, bool issequential){
	static uint32_t seq_cnt=0;
	std::set<uint32_t> * res=new std::set<uint32_t>();
	while(res->size()!=num_of_set){
		res->insert(issequential?seq_cnt++:rand()%(to-from+1)+from);
	}
	return res;
}

static inline std::set<uint32_t> *split_lba_set(std::set<uint32_t> *from, uint32_t num_of_set){
	std::set<uint32_t>::iterator iter;
	std::set<uint32_t> *res=new std::set<uint32_t>();
	uint32_t cnt;
	for(cnt=0, iter=from->begin(); iter!=from->end() && cnt<num_of_set; cnt++){
		res->insert(*iter);
		from->erase(iter++);
	}
	return res;
}

static char *flush_set(std::set<uint32_t> *target, bool map_write_same_block, uint32_t *map_ppa, uint32_t version_idx){
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

	for(iter=target->begin(); iter!=target->end(); iter++){
		inter_idx=cnt%L2PGAP;
		if(inter_idx==0){
			ppa=page_manager_get_new_ppa(LSM.pm, 
					false, map_write_same_block?DATASEG:SEPDATASEG);
			oob=LSM.pm->bm->get_oob(LSM.pm->bm, ppa);
		}
		mapping[cnt].lba=*iter;
		mapping[cnt].piece_ppa=ppa*L2PGAP+inter_idx;

		*(uint32_t*)&dummy_data[LPAGESIZE*(mapping[cnt].piece_ppa%L2PGAP)]=mapping[cnt].lba;

		version_coupling_lba_ridx(LSM.last_run_version, mapping[cnt].lba, version_idx);

		validate_piece_ppa(LSM.pm->bm, 1, &mapping[cnt].piece_ppa, 
				&mapping[cnt].lba, true);
		if(inter_idx==(L2PGAP-1)){
			io_manager_test_write(ppa, dummy_data, DATAW);
		}
		cnt++;
	}

	if(map_write_same_block){
		ppa=page_manager_get_new_ppa(LSM.pm, false, DATASEG);
		*map_ppa=ppa;
		if(ppa/_PPS!=mapping[cnt-1].piece_ppa/(L2PGAP*_PPS)){
			EPRINT("faile to assign ppa for block sst", true);
		}
		io_manager_test_write(ppa, key_ptr, DATAW);
	}
	delete target;
	return key_ptr;
}

