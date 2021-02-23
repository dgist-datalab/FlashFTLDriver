#ifndef __READ_HELPER_H__
#define __READ_HELPER_H__
#include "key_value_pair.h"
enum READ_HELPERTYPE{
	HELPER_NONE			=	0x0000000,
	HELPER_BF			=	0X0000001,
	HELPER_GUARD		=	0X0000002,
	HELPER_PLR			=	0x0000004,

	HELPER_BF_GUARD		=	HELPER_BF	| HELPER_GUARD,
	HELPER_BF_PLR		=	HELPER_BF	| HELPER_PLR,
	HELPER_ALL			=	HELPER_BF	| HELPER_PLR	| HELPER_GUARD,
};

typedef struct read_helper{
	uint32_t type;
	void *body;
}read_helper;

read_helper *read_helper_stream_init(uint32_t helper_type);
uint32_t read_helper_stream_insert(read_helper *, uint32_t helper_type, uint32_t lba, uint32_t ppa);

read_helper *read_helper_init(uint32_t helper_type, key_ptr_pair *kpp_array);
bool read_helper_check(read_helper *, uint32_t lba);
uint32_t read_helper_memory_usage(read_helper *);
void read_helper_print(read_helper *);
void read_helper_free(read_helper *);
read_helper *read_helper_copy(read_helper *);
void read_helper_copy(read_helper *des, read_helper *src);
#endif
