#ifndef __H_DESIGN_KNOB__
#define __H_DESIGN_KNOB__
#include "../../../include/settings.h"
typedef struct tlog{
	uint32_t ln;
	uint32_t tn;
	uint32_t border;
	uint32_t sf;
	uint32_t wiskey_line;
	uint32_t buffer_multiple;
}tlog_;

typedef struct tlog design_knob;
design_knob design_knob_find_opt(uint32_t memory_target);

#endif
