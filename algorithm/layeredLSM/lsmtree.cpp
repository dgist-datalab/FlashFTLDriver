#include "lsmtree.h"
#include "compaction.h"

page_read_buffer rb;

extern uint32_t test_key;
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

run *__lsm_populate_new_run(lsmtree *lsm, uint32_t map_type, uint32_t run_type, uint32_t entry_num, uint32_t level_num){
	fdriver_lock(&lsm->lock);
	uint32_t ridx=__rm_get_ridx(lsm->rm);
	run *r=run_factory(ridx, map_type, entry_num, lsm->param.fpr, lsm->bm, run_type, lsm);
	__rm_insert_run(lsm->rm, ridx, r);
	shortcut_add_run(lsm->shortcut, r, level_num);
	fdriver_unlock(&lsm->lock);
	return r;
}

bool __lsm_pinning_enable(lsmtree *lsm, uint32_t entry_num){
	uint64_t need_memory_bit=map_memory_per_ent(GUARD_BF, lsm->param.target_bit, lsm->param.fpr) *entry_num;
	need_memory_bit+=lsm->param.target_bit * entry_num;
	if(need_memory_bit+lsm->monitor.now_memory_usage <=lsm->param.max_memory_usage_bit){
		return true;
	}
	return false;
}

void __lsm_calculate_memory_usage(lsmtree *lsm, uint64_t entry_num, int32_t memory_usage_bit, uint32_t map_type, bool pinning){
	if(memory_usage_bit>=0){
		switch(map_type){
			case GUARD_BF:
				lsm->monitor.bf_memory_ent+=entry_num;
				lsm->monitor.bf_memory_usage+=memory_usage_bit-(pinning?lsm->param.target_bit*entry_num:0);
				break;
			case PLR_MAP:
				lsm->monitor.plr_memory_ent+=entry_num;
				lsm->monitor.plr_memory_usage+=memory_usage_bit;
				break;
		}
	}
	//if(memory_usage_bit)
	lsm->monitor.now_memory_usage+=memory_usage_bit;
	double mem_per_ent;
	if(memory_usage_bit<0){
		mem_per_ent=(double)memory_usage_bit/entry_num+(pinning?lsm->param.target_bit:0);
	}
	else{
		mem_per_ent=(double)memory_usage_bit/entry_num-(pinning?lsm->param.target_bit:0);
	}
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
	res->shortcut=shortcut_init(rm->total_run_num, RANGE, param.target_bit);
	res->bm=L2PBm_init(sm, rm->total_run_num);
	fdriver_mutex_init(&res->lock);

	param.spare_run_num-=MEMTABLE_NUM;
	param.memtable_entry_num=param.memtable_entry_num < _PPS*L2PGAP ? param.memtable_entry_num:param.memtable_entry_num/(_PPS*L2PGAP)*(_PPS*L2PGAP);
	res->memtable[0] = __lsm_populate_new_run(res, param.BF_level_range.start == 1 ? GUARD_BF : PLR_MAP,RUN_LOG, param.memtable_entry_num, 0);
	res->memtable[1]=NULL;

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
	res->tp=thpool_init(1);

	fdriver_mutex_init(&res->read_cnt_lock);
	res->now_flying_read_cnt=0;

	rb.pending_req=new std::multimap<uint32_t, algo_req *>();
	rb.issue_req=new std::multimap<uint32_t, algo_req*>();
	fdriver_mutex_init(&rb.pending_lock);
	fdriver_mutex_init(&rb.read_buffer_lock);
	rb.buffer_ppa=UINT32_MAX;
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
	delete rb.pending_req;
	delete rb.issue_req;
}

uint32_t lsmtree_insert(lsmtree *lsm, request *req){
	run *r=lsm->memtable[lsm->now_memtable_idx];

	if(req->key==test_key){
		printf("%u inserted from user\n", req->key);
	}

	fdriver_lock(&lsm->read_cnt_lock);
	if(lsm->now_flying_read_cnt && r->now_entry_num+1==r->max_entry_num){
		inf_assign_try(req);
		fdriver_unlock(&lsm->read_cnt_lock);
		return 1;
	}
	fdriver_unlock(&lsm->read_cnt_lock);

	//printf("req->key write:%u\n", req->key);
	run_insert(r, req->key, UINT32_MAX, req->value->value, false, 
		lsm->shortcut);

#ifdef SC_MEM_OPT
	if(shortcut_memory_full(lsm->shortcut) && lsm->shortcut->sc_dir[req->key/SC_PER_DIR].map_num > MAX_TABLE_NUM){
		//static uint32_t cnt=0;
		uint64_t before_memory_usage=lsm->shortcut->now_memory_usage;
		uint32_t move=run_reinsert(lsm, r, req->key/SC_PER_DIR*SC_PER_DIR, SC_PER_DIR, lsm->shortcut);
		uint64_t after_memory_usage=lsm->shortcut->now_memory_usage;
		//printf("reinsert cnt:%u\n",++cnt);
		//printf("%u sc memory diff:%lf, move:%u fool:%u\n", cnt++, (double)after_memory_usage/before_memory_usage,move, shortcut_memory_full(lsm->shortcut));
	}
#endif

	if(run_is_full(r)){
		run_insert_done(r, false);
#ifdef THREAD_COMPACTION
		if(thpool_num_threads_working(lsm->tp)==1){
			thpool_wait(lsm->tp);
		}
		thpool_add_work(lsm->tp,compaction_thread_run, (void*)r);
#else
		compaction_flush(lsm, r);
#endif
		//printf("free block num:%u\n", L2PBm_get_free_block_num(lsm->bm));
		lsm->now_memtable_idx=(lsm->now_memtable_idx+1)%MEMTABLE_NUM;
		lsm->memtable[lsm->now_memtable_idx]=__lsm_populate_new_run(lsm, lsm->param.BF_level_range.start==1?GUARD_BF:PLR_MAP, RUN_LOG, lsm->param.memtable_entry_num, 0);
		if(MEMTABLE_NUM!=1){
			lsm->memtable[(lsm->now_memtable_idx+1)%MEMTABLE_NUM]=NULL;
		}
	}
	return 0;
}

uint32_t lsmtree_read(lsmtree *lsm, request *req){
	uint32_t res;
	run *r;
	if(req->retry==false){
		r = shortcut_query(lsm->shortcut, req->key);
		//printf("req->key read:%u\n", req->key);
		if (r == NULL)
		{
			req->type = FS_NOTFOUND_T;
			//printf("req->key :%u not found\n", req->key);
			req->end_req(req);
			return READ_NOT_FOUND;
		}
		res = run_query(r, req);
	}
	else{
		r=((map_read_param*)req->param)->r;
		req->type_ftl++;
		res=run_query_retry(r, req);
	}
	if(res==READ_NOT_FOUND){
		req->type=FS_NOTFOUND_T;
		//printf("req->key :%u not found\n", req->key);
		req->end_req(req);
	}
	return res;
}

uint32_t lsmtree_print_log(lsmtree *lsm){
	printf("shortcut memory:%.2lf\n", (double)lsm->param.shortcut_bit/(RANGE*lsm->param.target_bit));
	printf("now_memory usage:%.2lf\n",(double)lsm->monitor.now_memory_usage/lsm->param.max_memory_usage_bit);
	printf("level memory:\n");
	uint64_t pftl_memory=lsm->param.target_bit*RANGE;
	uint64_t memtable_memory=0;
	for(uint32_t i=0; i<MEMTABLE_NUM; i++){
		memtable_memory+=run_memory_usage(lsm->memtable[i], lsm->param.target_bit);
	}
	printf("\tmemtable -> %.2lf\n",(double)memtable_memory/(pftl_memory));

	for(uint32_t i=0; i<lsm->param.total_level_num; i++){
		level *target_level=lsm->disk[i];
		uint64_t memory_usage_per_level=0;
		uint32_t pinning_run_num=0;
		for(uint32_t j=0; j<target_level->now_run_num; j++){
			run *target_run=target_level->run_array[j];
			if(!target_run) continue;
			if(target_run->type==RUN_PINNING){
				pinning_run_num++;
			}
			memory_usage_per_level+=run_memory_usage(target_run, lsm->param.target_bit);
		}
		printf("\t%u:%s %u(%u):%u -> %.2lf\n", i,
		map_type_to_string(target_level->map_type), target_level->now_run_num,
		pinning_run_num, target_level->max_run_num,
		(double)memory_usage_per_level/(pftl_memory));
	}

	printf("BF mem per ent: %.2lf\n",(double)lsm->monitor.bf_memory_usage/lsm->monitor.bf_memory_ent);
	printf("PLR mem per ent: %.2lf\n",(double)lsm->monitor.plr_memory_usage/lsm->monitor.plr_memory_ent);
	printf("SC mem per ent: %.4lf\n", (double)shortcut_memory_usage(lsm->shortcut)/RANGE);
#ifdef SC_MEM_OPT
	uint64_t table_num=0;
	for(uint32_t i=0; i<MAX_SC_DIR_NUM; i++){
		table_num+=lsm->shortcut->sc_dir[i].map_num;
	}
	printf("\t average table num:%lf\n",(double)table_num/MAX_SC_DIR_NUM);
#endif

	printf("compaction log\n");
	for(uint32_t i=0; i<=lsm->param.total_level_num; i++){
		if(i==0){
			printf("\tmem -> %u : %u,%.2lf (cnt,eff)\n",i, lsm->monitor.compaction_cnt[i], (double)lsm->monitor.compaction_output_entry_num[i]/lsm->monitor.compaction_input_entry_num[i]);
		}
		else{
			printf("\t%u -> %u : %u,%.2lf (cnt,eff)\n",i-1, i, lsm->monitor.compaction_cnt[i-1], (double)lsm->monitor.compaction_output_entry_num[i-1]/lsm->monitor.compaction_input_entry_num[i-1]);		
		}
	}
	printf("\t force_compaction_cnt:%u\n", lsm->monitor.force_compaction_cnt);

	return 1;
}

void lsmtree_run_print(lsmtree* lsm){
	for(uint32_t i=0; i<lsm->rm->total_run_num; i++){
		if(lsm->rm->run_array[i]){
			run_print(lsm->rm->run_array[i], false);
		}
	}
}
