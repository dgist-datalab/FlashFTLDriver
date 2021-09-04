#include "version.h"
#include "../../include/settings.h"
#include "lsmtree.h"
#include <math.h>

extern lsmtree LSM;

version *version_init(uint8_t total_version_number, uint32_t LBA_num, level **disk, uint32_t leveln){
	version *res=(version*)malloc(sizeof(version));
	res->start_hand=res->end_hand=0;
	res->key_version=(uint8_t*)calloc(LBA_num, sizeof(uint8_t));
	memset(res->key_version, -1, sizeof(uint8_t)*LBA_num);
	res->valid_version_num=0;
	res->max_valid_version_num=total_version_number;
	printf("max_version idx:%u\n", total_version_number);

	res->version_empty_queue=new std::list<uint32_t>**[leveln];
	res->start_vidx_of_level=(uint32_t*)malloc(sizeof(uint32_t)*leveln);
	res->level_run_num=(uint32_t*)malloc(sizeof(uint32_t)*leveln);
	res->poped_version_num=(uint32_t*)calloc(leveln, sizeof(uint32_t));

	res->V2O_map=(uint32_t**)malloc(sizeof(uint32_t*)*leveln);
	res->O2V_map=(uint32_t**)malloc(sizeof(uint32_t*)*leveln);
	res->R2O_map=(uint32_t**)malloc(sizeof(uint32_t*)*leveln);
	res->O2R_map=(uint32_t**)malloc(sizeof(uint32_t*)*leveln);

	res->level_order_token=(uint32_t*)calloc(leveln, sizeof(uint32_t));

	uint32_t version=0;
	for(int32_t i=leveln-1; i>=0; i--){
		res->version_empty_queue[i]=new std::list<uint32_t>*[QUEUE_TYPE_NUM];
		for(uint32_t j=0; j<QUEUE_TYPE_NUM; j++){
			res->version_empty_queue[i][j]=new std::list<uint32_t>();
		}

		res->start_vidx_of_level[i]=version;
		uint32_t limit=LSM.disk[i]->max_run_num;

		res->V2O_map[i]=(uint32_t*)calloc(limit,sizeof(uint32_t));
		res->O2V_map[i]=(uint32_t*)calloc(limit,sizeof(uint32_t));
		res->R2O_map[i]=(uint32_t*)calloc(limit,sizeof(uint32_t));
		res->O2R_map[i]=(uint32_t*)calloc(limit,sizeof(uint32_t));

		switch(disk[i]->level_type){
			case LEVELING:
			case LEVELING_WISCKEY:
				res->version_empty_queue[i][VERSION]->push_back(version++);
				res->version_empty_queue[i][RUNIDX]->push_back(0);
				res->level_run_num[i]=1;

				res->V2O_map[i][0]=0;
				res->O2V_map[i][0]=0;
				res->R2O_map[i][0]=0;
				res->O2R_map[i][0]=0;
				break;
			case TIERING_WISCKEY:
			case TIERING:
				res->level_run_num[i]=limit;
				for(uint32_t j=0; j<limit; j++){
					res->V2O_map[i][version-res->start_vidx_of_level[i]]=j;
					res->O2V_map[i][j]=version;
					res->R2O_map[i][j]=j;
					res->O2R_map[i][j]=j;

					res->version_empty_queue[i][VERSION]->push_back(version++);
					res->version_empty_queue[i][RUNIDX]->push_back(j);
				}
				break;
		}
	}

	res->version_invalidate_number=(uint32_t*)calloc(total_version_number+1, sizeof(uint32_t));
	res->version_populate_queue=new std::list<uint32_t>**[leveln];
	for(uint32_t i=0; i<leveln; i++){
		res->version_populate_queue[i]=new std::list<uint32_t>*[QUEUE_TYPE_NUM];
		for(uint32_t j=0; j<QUEUE_TYPE_NUM; j++){
			res->version_populate_queue[i][j]=new std::list<uint32_t>();
		}
	}

	res->total_version_number=total_version_number;
	//res->version_early_invalidate=(bool*)calloc(res->total_version_number+1, sizeof(bool));
	res->memory_usage_bit=ceil(log2(res->max_valid_version_num))*LBA_num;
	fdriver_mutex_init(&res->version_lock);
	res->leveln=leveln;

	return res;
}

uint32_t version_get_empty_version(version *v, uint32_t level_idx){
	if(v->version_empty_queue[level_idx][VERSION]->empty()){
		EPRINT("should merge before empty version", true);
	}
	uint32_t res=v->version_empty_queue[level_idx][VERSION]->front();
	v->version_empty_queue[level_idx][VERSION]->pop_front();

	/*update mapping*/
	uint32_t ridx=v->version_empty_queue[level_idx][RUNIDX]->front();
	v->version_empty_queue[level_idx][RUNIDX]->pop_front();
	uint32_t order=v->level_order_token[level_idx];
	if(order >= v->level_run_num[level_idx]){
		EPRINT("order should be less then max run_num", true);
	}
	uint32_t vidx=res-v->start_vidx_of_level[level_idx];

	v->V2O_map[level_idx][vidx]=order;
	v->O2V_map[level_idx][order]=vidx;
	v->R2O_map[level_idx][ridx]=order;
	v->O2R_map[level_idx][order]=ridx;

	v->level_order_token[level_idx]++;
	return res;
}

void version_get_merge_target(version *v, uint32_t *version_set, uint32_t level_idx){
	uint32_t start_ver=version_level_to_start_version(LSM.last_run_version, level_idx);
	uint32_t end_ver=version_level_to_start_version(LSM.last_run_version, level_idx-1)-1;

	int32_t most_version=-1;
	int32_t most_invalid_num=0;
	float most_invalid_ratio=0;
	int32_t sec_most_version=-1;


#ifdef INVALIDATION_COUNT_MERGE
	for(uint32_t i=start_ver; i<=end_ver; i++){
		float now_invalid_ratio=(float)v->version_invalidate_number[i]/LSM.disk[level_idx]->array[i].now_contents_num;

		if(most_invalid_ratio < now_invalid_ratio){
			most_invalid_ratio=now_invalid_ratio;
			sec_most_version=most_version;
			most_version=i;
		}
/*
		if(most_invalid_num < v->version_invalidate_number[i]){
			most_invalid_num=v->version_invalidate_number[i];
			sec_most_version=most_version;
			most_version=i;
		}
*/
	}
#endif

	if(most_version==-1 || sec_most_version==-1){
		std::list<uint32_t>::iterator iter=v->version_populate_queue[level_idx][VERSION]->begin();
		version_set[0]=*iter;
		iter++;
		version_set[1]=*iter;
	}
	else{
		version_set[0]=most_version;
		version_set[1]=sec_most_version;
	}
}

void version_update_mapping_merge(version *v, uint32_t *version_set, uint32_t level_idx){
	uint32_t most_version=version_set[0];
	uint32_t sec_most_version=version_set[1];

	uint32_t big_order=MAX(v->V2O_map[level_idx][most_version],v->V2O_map[level_idx][sec_most_version]);
	uint32_t small_order=MIN(v->V2O_map[level_idx][most_version],v->V2O_map[level_idx][sec_most_version]);

	/*change version and ridx*/
	for(uint32_t i=0; i<v->level_run_num[level_idx]; i++){
		uint32_t decrease_num=0;
		if(i==big_order || i==small_order) continue;
		if(i > big_order) decrease_num++;
		if(i > small_order) decrease_num++;
		
		if(decrease_num==0) continue;
		uint32_t target_vidx=v->O2V_map[level_idx][i];
		v->V2O_map[level_idx][target_vidx]=i-decrease_num;
		v->O2V_map[level_idx][i-decrease_num]=target_vidx;

		uint32_t target_ridx=v->O2R_map[level_idx][i];
		v->R2O_map[level_idx][target_ridx]=i-decrease_num;
		v->O2R_map[level_idx][i-decrease_num]=target_ridx;
	}

	v->level_order_token[level_idx]-=MERGED_RUN_NUM;
}

void version_clear_merge_target(version *v, uint32_t *version_set, uint32_t level_idx){
	for(uint32_t i=0; i<MERGED_RUN_NUM; i++){
		uint32_t target_version=version_set[i];

		v->version_invalidate_number[target_version]=0;

		std::list<uint32_t>::iterator v_iter=v->version_populate_queue[level_idx][VERSION]->begin();
		std::list<uint32_t>::iterator r_iter=v->version_populate_queue[level_idx][RUNIDX]->begin();

		for(; v_iter!=v->version_empty_queue[level_idx][VERSION]->end(); ){
			if(*v_iter==target_version){		
				v->version_empty_queue[level_idx][VERSION]->push_back(*v_iter);
				v->version_empty_queue[level_idx][RUNIDX]->push_back(*r_iter);

				v->version_populate_queue[level_idx][VERSION]->erase(v_iter);
				v->version_populate_queue[level_idx][RUNIDX]->erase(r_iter);			
				break;
			}
			else{
				v_iter++;
				r_iter++;
			}
		}
	}
	version_update_mapping_merge(v, version_set, level_idx);
}

static void version_order_update(version *v, uint32_t target_version, uint32_t level_idx){
	uint32_t target_order=v->V2O_map[level_idx][target_version];
	for(uint32_t i=0; i<v->level_run_num[level_idx]; i++){
		uint32_t decrease_num=0;
		if(i==target_version) continue;
		if(i > target_version) decrease_num++;

		if(decrease_num==0) continue;
		uint32_t target_vidx=v->O2V_map[level_idx][i];
		v->V2O_map[level_idx][target_vidx]=i-decrease_num;
		v->O2V_map[level_idx][i-decrease_num]=target_vidx;

		uint32_t target_ridx=v->O2R_map[level_idx][i];
		v->R2O_map[level_idx][target_ridx]=i-decrease_num;
		v->O2R_map[level_idx][i-decrease_num]=target_ridx;
	}
	v->level_order_token[level_idx]-=1;
}

void version_clear_target(version *v, uint32_t target_version, uint32_t level_idx){
	v->version_invalidate_number[target_version]=0;

	std::list<uint32_t>::iterator v_iter=v->version_populate_queue[level_idx][VERSION]->begin();
	std::list<uint32_t>::iterator r_iter=v->version_populate_queue[level_idx][RUNIDX]->begin();

	for(; v_iter!=v->version_empty_queue[level_idx][VERSION]->end(); ){
		if(*v_iter==target_version){		
			v->version_empty_queue[level_idx][VERSION]->push_back(*v_iter);
			v->version_empty_queue[level_idx][RUNIDX]->push_back(*r_iter);

			v->version_populate_queue[level_idx][VERSION]->erase(v_iter);
			v->version_populate_queue[level_idx][RUNIDX]->erase(r_iter);			
			break;
		}
		else{
			v_iter++;
			r_iter++;
		}
	}
	version_order_update(v, target_version, level_idx);
}

void version_repopulate_merge_target(version *v, uint32_t target_version, uint32_t ridx, uint32_t level_idx){
	uint32_t get_version=v->version_empty_queue[level_idx][VERSION]->front();
	uint32_t get_ridx=v->version_empty_queue[level_idx][RUNIDX]->front();

	if(target_version!=get_version || ridx!=get_ridx){
		EPRINT("version or ridx error", true);
	}

	uint32_t order=v->level_order_token[level_idx];
	
	if(order!=v->level_run_num[level_idx]-2){
		EPRINT("order error", true);
	}


	uint32_t vidx=target_version-v->start_vidx_of_level[level_idx];

	v->V2O_map[level_idx][vidx]=order;
	v->O2V_map[level_idx][order]=vidx;
	v->R2O_map[level_idx][ridx]=order;
	v->O2R_map[level_idx][order]=ridx;

	v->level_order_token[level_idx]++;

	v->version_empty_queue[level_idx][VERSION]->pop_front();
	v->version_empty_queue[level_idx][RUNIDX]->pop_front();

	v->version_populate_queue[level_idx][VERSION]->push_back(get_version);
	v->version_populate_queue[level_idx][RUNIDX]->push_back(get_ridx);
}

static inline void version_unpopulate(version *v, uint32_t version, uint32_t level_idx){
	v->version_empty_queue[level_idx][VERSION]->push_back(version);
	v->version_empty_queue[level_idx][RUNIDX]->push_back(version_to_ridx(v, level_idx, version));
	v->version_invalidate_number[version]=0;
}

void version_reunpopulate(version *v, uint32_t version, uint32_t level_idx){
	v->version_empty_queue[level_idx][VERSION]->push_front(version);
	v->version_empty_queue[level_idx][RUNIDX]->push_front(version_to_ridx(v, level_idx, version));
	v->version_invalidate_number[version]=0;
	v->level_order_token[level_idx]--;
}
void version_clear_level(version *v, uint32_t level_idx){
	uint32_t max_run_num=v->version_populate_queue[level_idx][VERSION]->size();
	if(max_run_num==v->level_run_num[level_idx]){
		for(uint32_t i=0; i<max_run_num; i++){
			version_unpopulate(v,
					version_pop_oldest_version(LSM.last_run_version, level_idx),
					level_idx);
		}
	}
	else{
		v->version_empty_queue[level_idx][VERSION]->clear();
		v->version_empty_queue[level_idx][RUNIDX]->clear();

		v->version_populate_queue[level_idx][VERSION]->clear();
		v->version_populate_queue[level_idx][RUNIDX]->clear();

		uint32_t limit=LSM.disk[level_idx]->max_run_num;
		uint32_t version=v->start_vidx_of_level[level_idx];
		for(uint32_t j=0; j<limit; j++){
			v->V2O_map[level_idx][version-v->start_vidx_of_level[level_idx]]=j;
			v->O2V_map[level_idx][j]=version;
			v->R2O_map[level_idx][j]=j;
			v->O2R_map[level_idx][j]=j;

			v->version_empty_queue[level_idx][VERSION]->push_back(version++);
			v->version_empty_queue[level_idx][RUNIDX]->push_back(j);
		}
	}
	v->level_order_token[level_idx]=0;
}

void version_populate(version *v, uint32_t version, uint32_t level_idx){
	v->version_populate_queue[level_idx][VERSION]->push_back(version);
	v->version_populate_queue[level_idx][RUNIDX]->push_back(version_to_ridx(v, level_idx, version));
}

void version_sanity_checker(version *v){
#ifdef LSM_DEBUG
	uint32_t remain_empty_size=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		remain_empty_size+=v->version_empty_queue[i][VERSION]->size();
	}

	uint32_t populate_size=0;
	for(uint32_t i=0; i<LSM.param.LEVELN; i++){
		populate_size+=v->version_populate_queue[i][VERSION]->size();
	}
	if(remain_empty_size+populate_size!=v->max_valid_version_num){
		printf("error log : empty-size(%d) populate-size(%d)\n", remain_empty_size, populate_size);
		for(uint32_t i=0; i<LSM.param.LEVELN; i++){
			printf("%u: %lu %lu\n", i, v->version_empty_queue[i][VERSION]->size(), 
					v->version_populate_queue[i][VERSION]->size());
		}
		EPRINT("version sanity error", true);
	}
#endif
}

void version_print_order(version *v, uint32_t lev_idx){
	printf("version lev:%u print, poped_version_num:%u\n", lev_idx, v->poped_version_num[lev_idx]);
	uint32_t start_version=version_pick_oldest_version(v, lev_idx);
	printf("vidx version ridx\n");
	uint32_t run_num=v->level_run_num[lev_idx];
	for(uint32_t i=0; i<run_num; i++){
		uint32_t target_version=(start_version+i)%(v->level_run_num[lev_idx])+v->start_vidx_of_level[lev_idx];
		uint32_t test=version_to_order(v, lev_idx, target_version);

		if(i!=test){
			printf("%u - %u\n", i, test);
			EPRINT("order is differ", true);
		}
		uint32_t ridx=version_order_to_ridx(v, lev_idx, i);
		printf("%u %u %u\n", i, target_version, ridx);
	}
}

void version_free(version *v){
	for(uint32_t i=0; i<v->leveln; i++){
		for(uint32_t j=0; j<QUEUE_TYPE_NUM; j++){
			delete v->version_empty_queue[i][j];
			delete v->version_populate_queue[i][j];
		}
		delete[] v->version_empty_queue[i];
		delete[] v->version_populate_queue[i];
	}
	delete[] v->version_empty_queue;
	delete[] v->version_populate_queue;

	free(v->level_run_num);
	free(v->poped_version_num);
	free(v->start_vidx_of_level);
	//free(v->version_early_invalidate);
	free(v->key_version);
	free(v);
}

extern uint32_t debug_lba;
void version_coupling_lba_version(version *v, uint32_t lba, uint8_t version){
	if(version!=UINT8_MAX && version>v->total_version_number){
		EPRINT("over version num", true);
	}
#ifdef LSM_DEBUG
	if(lba==debug_lba){
		printf("[version_map] lba:%u->%u lev:%u\n",lba, version, 
				version_to_level_idx(v, version, v->leveln));
	}
#endif
	fdriver_lock(&v->version_lock);
	if(v->key_version[lba]!=UINT8_MAX && v->key_version[lba]!=version){
		v->version_invalidate_number[v->key_version[lba]]++;
	}

	v->key_version[lba]=version;
	fdriver_unlock(&v->version_lock);
}

uint32_t version_decrease_invalidation_number(version *v, uint32_t version_number, 
		int32_t decrease_num){
	if((int32_t)v->version_invalidate_number[version_number] < decrease_num){
		EPRINT("version invalidate nubmer should be natuer number", true);
	}
	v->version_invalidate_number[version_number]-=decrease_num;
	return 1;
}
/*
void version_reinit_early_invalidation(version *v, uint32_t version_num, uint32_t *version){
	EPRINT("should I need it?\n", false);
	for(uint32_t i=0; i<version_num; i++){
		v->version_invalidation_cnt[version[i]]=0;
		v->version_early_invalidate[version[i]]=false;
	}
}

uint32_t version_get_early_invalidation_target(version *v){
	EPRINT("should I need it?\n", false);

	for(uint32_t i=0; i<v->max_valid_version_num; i++){
		if(v->version_early_invalidate[version_to_run(v,i)]){
			return version_to_run(v,i);
		}
	}
	return UINT32_MAX;
}

static bool early_invalidate_available_check(uint32_t version){
	EPRINT("should I need it?\n", false);
	run *r=&LSM.disk[LSM.param.LEVELN-1]->array[version];
	sst_file *sptr;
	map_range *mptr;
	uint32_t sidx, midx;
	for_each_sst(r, sptr, sidx){
		for_each_map_range(sptr, mptr, midx){
			if(LSM.gc_unavailable_seg[mptr->ppa/_PPS]){
				return false;
			}
		}
	}
	return true;
}

uint32_t version_get_max_invalidation_target(version *v, uint32_t *invalidated_num, uint32_t *avg_invalidated_num){
	EPRINT("should I need it?\n", false);
	uint32_t target_version=UINT32_MAX;
	uint32_t target_invalidation_cnt=0;
	for(uint32_t i=v->last_level_version_num; i<v->max_valid_version_num; i++){
		if(v->version_early_invalidate[i]) continue;
		if(LSM.now_merging_run[0]==i || LSM.now_merging_run[1]==i) continue;
		if(target_invalidation_cnt<v->version_invalidation_cnt[i]){
			if(early_invalidate_available_check(i)){
				target_version=i;
				target_invalidation_cnt=v->version_invalidation_cnt[i];
			}	
		}
	}
	if(target_version==UINT32_MAX){
		return target_version;
	}

	if(invalidated_num){
		*invalidated_num=target_invalidation_cnt;
	}
	if(avg_invalidated_num){
		*avg_invalidated_num=(target_invalidation_cnt/
			LSM.disk[LSM.param.LEVELN-1]->array[target_version].now_sst_num);
	}

	return target_version;
}
*/
uint32_t version_update_for_trivial_move(version *v, uint32_t start_lba, uint32_t end_lba, 
		uint32_t src_level_idx, uint32_t des_level_idx, uint32_t target_version){
	for(uint32_t i=start_lba; i<=end_lba; i++){
		if(src_level_idx==UINT32_MAX){
			uint8_t now_version=version_map_lba(v,i);
			if(now_version!=(uint8_t)-1){
				continue;
			}
			version_coupling_lba_version(v, i, target_version);
		}
		else if(version_to_level_idx(v, version_map_lba(v, i), v->leveln)==src_level_idx){
			version_coupling_lba_version(v, i, target_version);
		}
		else{
		}
	}
	return 1;
}
/*
void version_make_early_invalidation_enable_old(version *v){
	EPRINT("should I need it?\n", false);
	for(uint32_t i=0; i<v->max_valid_version_num-1; i++){
		v->version_early_invalidate[version_to_run(v,i)]=true;
	}
}
*/
uint32_t version_level_to_start_version(version *v, uint32_t lev_idx){
	return v->start_vidx_of_level[lev_idx];
}
/*
void version_traversal(version *v){
	uint32_t* version_array=(uint32_t*)malloc(sizeof(uint32_t)*v->max_valid_version_num);
	for(uint32_t i=0; i<v->max_valid_version_num; i++){
		version_array[i]=i;
	}

	for(uint32_t i=0; i<v->max_valid_version_num-1; i++){
		uint32_t a=version_array[i];
		for(uint32_t j=i+1; j<v->max_valid_version_num; j++){
			uint32_t b=version_array[j];
			if(version_compare(v, a, b) > 0){
				uint32_t temp=version_array[i];
				version_array[i]=version_array[j];
				version_array[j]=temp;
			}
		}
	}

	printf("version order print\n");
	for(uint32_t i=0; i<v->max_valid_version_num; i++){
		printf("%u - %u\n",i, version_array[i]);
	}

	free(version_array);
}
*/
uint32_t version_get_level_invalidation_cnt(version *v, uint32_t level_idx){
	if(level_idx==0) return 0;
	uint32_t start_idx=version_level_to_start_version(v, level_idx);
	uint32_t end_idx=version_level_to_start_version(v, level_idx-1);
	uint32_t res=0;
	for(uint32_t i=start_idx; i<end_idx; i++){
		res+=v->version_invalidate_number[i];
	}
	return res;
}

uint32_t version_get_resort_version(version *v, uint32_t level_idx){
//	uint32_t remain_size=v->version_empty_queue[level_idx]->size();
//	for(uint32_t i=0; i<remain_size; i++){
//		uint32_t target_version=version_get_empty_version(v, level_idx);
//		version_populate(v, target_version, level_idx);
//	}
//
//	remain_size=v->version_populate_queue[level_idx]->size();
//	for(uint32_t i=0; i<remain_size; i++){
//		version_unpopulate(v, version_pop_oldest_version(v, level_idx), level_idx);
//	}


	return 1;
}
