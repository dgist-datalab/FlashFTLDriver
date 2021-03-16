#ifndef __READ_HELPER_H__
#define __READ_HELPER_H__
#include "key_value_pair.h"
#include "../../include/settings.h"
#include "sst_file.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum READ_HELPERTYPE{
	HELPER_BF_PTR			=	0x0000001,
	HELPER_BF_ONLY			=   0x0000002,
	HELPER_GUARD			=	0x0000004,
	HELPER_PLR				=	0x0000008,

	HELPER_BF_PTR_GUARD 		=	HELPER_BF_PTR	| HELPER_GUARD,
	HELPER_BF_ONLY_GUARD		=	HELPER_BF_ONLY	| HELPER_GUARD,
	HELPER_BF_PLR				=	HELPER_BF_PTR	| HELPER_PLR,
	HELPER_ALL					=	HELPER_BF_PTR	| HELPER_PLR	| HELPER_GUARD,
};

typedef struct read_helper{
	uint32_t type;
	void *body;
}read_helper;

typedef struct read_helper_param{
	uint32_t type;
	float target_prob;
	uint32_t member_num;
}read_helper_param;

//read_helper *read_helper_stream_init(uint32_t helper_type);

read_helper *read_helper_init(read_helper_param rhp);
read_helper *read_helper_kpset_to_rh(read_helper_param rhp, key_ptr_pair *kp_set);
uint32_t read_helper_stream_insert(read_helper *, uint32_t lba, uint32_t piece_ppa);
bool read_helper_check(read_helper *, uint32_t lba, uint32_t *piece_ppa_result, struct sst_file *, uint32_t *idx);
uint32_t read_helper_memory_usage(read_helper *);
void read_helper_print(read_helper *);
void read_helper_free(read_helper *);
void read_helper_copy(read_helper *des, read_helper *src);
void read_helper_move(read_helper *des, read_helper *src);
bool read_helper_last(read_helper *rh, uint32_t idx);
uint32_t read_helper_idx_init(read_helper *rh, uint32_t lba);
#endif
