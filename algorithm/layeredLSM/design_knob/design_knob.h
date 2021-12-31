#ifndef __H_DESIGN_KNOB__
#define __H_DESIGN_KNOB__
#include "../../../include/settings.h"
#include "../../../include/search_template.h"
typedef struct tlog{
	uint32_t ln;
	uint32_t tn;
	uint32_t border;
	uint32_t sf;
	uint32_t wiskey_line;
	uint32_t buffer_multiple;
}tlog_;

typedef struct tlog design_knob;
void init_memory_info(uint32_t error, uint32_t target_bit);
double bf_memory_per_ent(double ratio, uint32_t target_bit);
double plr_memory_per_ent(double ratio, uint32_t target_bit);
double get_line_per_chunk(double ratio);
double bf_advance_ratio(uint32_t target_bit);
uint64_t line_cnt(double ratio);
uint64_t chunk_cnt(double ratio);
void destroy_memory_info();
#endif
