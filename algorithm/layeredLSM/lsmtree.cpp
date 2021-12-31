#include "lsmtree.h"
#include "compaction.h"

static inline uint32_t __rm_get_ridx(run_manager *rm){
	if(rm->ridx_queue->empty()){
		EPRINT("empty ridx queue", true);
	}
	uint32_t res=rm->ridx_queue->front();
	rm->ridx_queue->pop();
	return res;
}

static inline void __rm_insert_run(run_manager *rm, uint32_t ridx, run *r){
	rm->run_array[ridx]=r;
}

static inline void __rm_free_ridx(run_manager *rm, uint32_t ridx){
	rm->run_array[ridx]=NULL;
	rm->ridx_queue->push(ridx);
}

run *__lsm_populate_new_run(lsmtree *lsm, uint32_t map_type, uint32_t run_type, uint32_t entry_num){
	uint32_t ridx=__rm_get_ridx(lsm->rm);
	run *r=run_factory(ridx, map_type, entry_num, lsm->param.fpr, lsm->bm, run_type, lsm);
	__rm_insert_run(lsm->rm, ridx, r);
	shortcut_add_run(lsm->shortcut, r);
	return r;
}

bool __lsm_pinning_enable(lsmtree *lsm, uint32_t entry_num){
	uint64_t need_memory_bit=map_memory_per_ent(GUARD_BF, lsm->param.target_bit, lsm->param.fpr);
	if(need_memory_bit+lsm->monitor.now_memory_usage <=lsm->param.max_memory_usage_bit){
		return true;
	}
	return false;
}

void __lsm_calculate_memory_usage(lsmtree *lsm, int32_t memory_usage_bit){
	lsm->monitor.now_memory_usage+=memory_usage_bit;
}

void __lsm_free_run(lsmtree *lsm, run *r){
	__rm_free_ridx(lsm->rm, r->run_idx);
	run_free(r, lsm->shortcut);
}

lsmtree* lsmtree_init(lsmtree_parameter param, blockmanager *sm){
	run_manager *rm=(run_manager *)malloc(sizeof(run_manager));
	lsmtree *res=(lsmtree*)calloc(1, sizeof(lsmtree));

	rm->total_run_num=param.total_run_num+param.spare_run_num;
	rm->run_array=(run**)calloc(rm->total_run_num, sizeof(run*));
	rm->ridx_queue=new std::queue<uint32_t>();
	
	for(uint32_t i=0; i<rm->total_run_num; i++){
		rm->ridx_queue->push(i);
	}
	res->rm=rm;
	res->param=param;
	sorted_array_master_init(rm->total_run_num);
	res->shortcut=shortcut_init(rm->total_run_num, RANGE);
	res->bm=L2PBm_init(sm, rm->total_run_num);

	param.spare_run_num-=MEMTABLE_NUM;
	param.memtable_entry_num=param.memtable_entry_num < _PPS*L2PGAP ? param.memtable_entry_num:param.memtable_entry_num/(_PPS*L2PGAP)*(_PPS*L2PGAP);
	for(uint32_t i=0; i<MEMTABLE_NUM; i++){
		res->memtable[i]=__lsm_populate_new_run(res, TREE_MAP, RUN_LOG, param.memtable_entry_num);
	}

	param.spare_run_num-=1; //spare_run for compaction_target
	res->disk=(level **)calloc(param.total_level_num, sizeof(level*));
	for(uint32_t i=0; i<param.total_level_num; i++){
		uint32_t run_num= i==param.total_level_num-1?param.size_factor + param.spare_run_num:
			param.size_factor;
		bool isbf=true;
		if(i > param.BF_level_range.end-1){
			isbf=false;
		}
		res->disk[i]=level_init(i, run_num, isbf? GUARD_BF: PLR_MAP);
	}

	res->monitor.now_memory_usage+=res->param.shortcut_bit;

	return res;
}

void lsmtree_free(lsmtree *lsm){
	for(uint32_t i=0; i<lsm->rm->total_run_num; i++){
		if(lsm->rm->run_array[i]){
			__lsm_free_run(lsm, lsm->rm->run_array[i]);
		}
	}
	delete lsm->rm->ridx_queue;
	free(lsm->rm->run_array);
	free(lsm->rm);

	for(uint32_t i=0; i<lsm->param.total_level_num; i++){
		level_free(lsm->disk[i]);
	}

	free(lsm->disk);
	sorted_array_master_free();
	shortcut_free(lsm->shortcut);
	L2PBm_free(lsm->bm);
	free(lsm);
}

uint32_t lsmtree_insert(lsmtree *lsm, request *req){
	run *r=lsm->memtable[lsm->now_memtable_idx];
	run_insert(r, req->key, UINT32_MAX, req->value->value, false, 
		lsm->shortcut);

	if(run_is_full(r)){
		run_insert_done(r, false);
		lsm->now_memtable_idx=(lsm->now_memtable_idx+1)%MEMTABLE_NUM;
		compaction_flush(lsm, r);
		lsm->memtable[(lsm->now_memtable_idx+1)%MEMTABLE_NUM]=__lsm_populate_new_run(lsm, TREE_MAP, RUN_LOG, lsm->param.memtable_entry_num);
	}
	return 0;
}

uint32_t lsmtree_read(lsmtree *lsm, request *req){
	uint32_t res;
	run *r=shortcut_query(lsm->shortcut, req->key);
	if(!req->retry){
		res=run_query(r, req);
	}
	else{
		res=run_query_retry(r, req);
	}

	return res;
}
