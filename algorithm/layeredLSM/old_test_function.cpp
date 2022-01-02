#ifdef 0
uint32_t test_function_pinning_test(){
	uint32_t target_entry_num=_PPB*L2PGAP*4+_PPB*L2PGAP/2;
	run *a=__lsm_populate_new_run(LSM, TREE_MAP, RUN_LOG, target_entry_num);
	run *b=__lsm_populate_new_run(LSM, TREE_MAP, RUN_LOG, target_entry_num);

	uint32_t temp_lba=0;
	for(uint32_t i=0; i<9; i++){
		run *target=i>4?a:b;
		uint32_t target_num=i==3?_PPB*L2PGAP/2:_PPB*L2PGAP;
		for(uint32_t j=0; j<target_num; j++){
			request *req=__make_dummy_request(temp_lba++,false);
			run_insert(target, req->key, UINT32_MAX, req->value->value, false, LSM->shortcut);
		}
	}

	printf("max insert lba:%u\n", temp_lba-1);

	run_insert_done(a, false);
	run_insert_done(b, false);

	run *merge_set[2];
	merge_set[0]=a;
	merge_set[1]=b;
	run *target=__lsm_populate_new_run(LSM, EXACT, RUN_PINNING, target_entry_num*2);
	run_merge(2,merge_set, target, LSM, false);


	for(uint32_t i=0; i<temp_lba; i++){
		request *req=__make_dummy_request(i, true);
		lsmtree_read(LSM, req);
	}
	return 1;
}
uint32_t test_function_sparse_middle(){
	uint32_t target_entry_num=_PPB*L2PGAP*4+_PPB*L2PGAP/2;
	run *a=__lsm_populate_new_run(LSM, GUARD_BF, RUN_NORMAL, target_entry_num);
	run *b=__lsm_populate_new_run(LSM, GUARD_BF, RUN_NORMAL, target_entry_num);

	uint32_t temp_lba=0;
	for(uint32_t i=0; i<9; i++){
		run *target=i>4?a:b;
		uint32_t target_num=i==3?_PPB*L2PGAP/2:_PPB*L2PGAP;
		for(uint32_t j=0; j<target_num; j++){
			request *req=__make_dummy_request(temp_lba++,false);
			run_insert(target, req->key, UINT32_MAX, req->value->value, false, LSM->shortcut);
		}
	}

	printf("max insert lba:%u\n", temp_lba-1);

	run_insert_done(a, false);
	run_insert_done(b, false);

	run *merge_set[2];
	merge_set[0]=a;
	merge_set[1]=b;
	run *target=__lsm_populate_new_run(LSM, EXACT, RUN_NORMAL, target_entry_num*2);
	run_merge(2,merge_set, target, LSM, false);


	for(uint32_t i=0; i<temp_lba; i++){
		request *req=__make_dummy_request(i, true);
		lsmtree_read(LSM, req);
	}
	return 1;
}

uint32_t test_function(){
	uint32_t target_entry_num=_PPB*L2PGAP*4+_PPB*L2PGAP/2;
	run *a=__lsm_populate_new_run(LSM, GUARD_BF, RUN_NORMAL, target_entry_num);
	run *b=__lsm_populate_new_run(LSM, GUARD_BF, RUN_NORMAL, target_entry_num);

	uint32_t temp_lba=0;
	for(uint32_t i=0; i<9; i++){
		run *target=i%2==0?a:b;
		uint32_t target_num=i>=7?_PPB*L2PGAP/2:_PPB*L2PGAP;
		for(uint32_t j=0; j<target_num; j++){
			request *req=__make_dummy_request(temp_lba++,false);
			run_insert(target, req->key, UINT32_MAX, req->value->value, false, LSM->shortcut);
		}
	}

	printf("max insert lba:%u\n", temp_lba-1);

	run_insert_done(a, false);
	run_insert_done(b, false);

	run *merge_set[2];
	merge_set[0]=a;
	merge_set[1]=b;
	run *target=__lsm_populate_new_run(LSM, EXACT, RUN_NORMAL, target_entry_num*2);
	run_merge(2,merge_set, target, LSM, false);

	for(uint32_t i=0; i<temp_lba; i++){
		request *req=__make_dummy_request(i, true);
		lsmtree_read(LSM, req);
	}
	return 1;
}
#endif