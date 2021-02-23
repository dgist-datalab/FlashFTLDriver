#include "read_helper.h"
read_helper *read_helper_stream_init(uint32_t helper_type){
	read_helper *res=(read_helper*)malloc(sizeof(read_helper));
	res->type=helper_type;
	switch(helper_type){
		case HELPER_BF:
			EPINRT("now_testing fpr 0.1", false);
			res->body=(void*)bf_init(PAGESIZE/sizeof(key_ptr_pair), fpr);
//			bf_set((BF*)helper->body, lba);
			break;
		case HELPER_GUARD:
		case HELPER_PLR:
		case HELPER_BF_PLR:
		case HELPER_BF_GUARD:
		case HELPER_ALL:
			EPINRT("no implement\n",true);
			break;
		case HELPER_NONE:
			break;
	}
	return res;
}

uint32_t read_helper_stream_insert(read_helper *helper, uint32_t helper_type, uint32_t lba, uint32_t ppa){
	if(helper->type!=HELPER_NONE && !helper->body){
		EPINRT("allocate body before insert", true);
	}

	switch(helper->type){
		case HELPER_BF:
			bf_set((BF*)helper->body, lba);
			break;
		case HELPER_GUARD:
		case HELPER_PLR:
		case HELPER_BF_PLR:
		case HELPER_BF_GUARD:
		case HELPER_ALL:
			EPINRT("no implement\n",true);
			break;
		case HELPER_NONE:
			break;
	}
	return 1;
}
/*
read_helper *read_helper_init(uint32_t helper_type, key_ptr_pair *kpp_array){
	read_helper *res=(read_helper*)malloc(sizeof(read_helper));
	res->type=helper_type;
	switch(helper_type){
		case HELPER_BF:
			EPINRT("now_testing fpr 0.1", false);
			res->body=(void*)bf_init(PAGESIZE/sizeof(key_ptr_pair), fpr);
//			bf_set((BF*)helper->body, lba);
			break;
		case HELPER_GUARD:
		case HELPER_PLR:
		case HELPER_BF_PLR:
		case HELPER_BF_GUARD:
		case HELPER_ALL:
			EPINRT("no implement\n",true);
			break;
		case HELPER_NONE:
			break;
	}
	return res;
}*/

bool read_helper_check(read_helper *helper, uint32_t lba){
	switch(helper->helper_type){
		case HELPER_BF;
			return bf_check((BF*)helper->body, lba);
		case HELPER_GUARD:
		case HELPER_PLR:
		case HELPER_BF_PLR:
		case HELPER_BF_GUARD:
		case HELPER_ALL:
			EPINRT("no implement\n",true);
			break;
		case HELPER_NONE:
			break;
	}
	return true;
}

uint32_t read_helper_memory_usage(read_helper *helper){
	switch(helper->helper_type){
		case HELPER_BF;
			return bf_check((BF*)helper->body, lba);
		case HELPER_GUARD:
		case HELPER_PLR:
		case HELPER_BF_PLR:
		case HELPER_BF_GUARD:
		case HELPER_ALL:
			EPINRT("no implement\n",true);
			break;
		case HELPER_NONE:
			break;
	}
	return true;
}

void read_helper_print(read_helper *helper){
	EPINRT("not implemented", false);
	return;
}

void read_helper_free(read_helper *){
	EPINRT("not implemented", false);
	return;
}

read_helper *read_helper_copy(read_helper *){
	EPINRT("not implemented", false);
	return;
}

void read_helper_copy(read_helper *des, read_helper *src){
	EPINRT("not implemented", false);
	return;
}
