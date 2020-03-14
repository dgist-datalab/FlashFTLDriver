#ifndef __H_VARIABLE__
#define __H_VARIABLE__
#include "../../include/container.h"
#include "../../include/settings.h"
#include "../../interface/interface.h"
#include "skiplist.h"
void *variable_value2Page(level *in,l_bucket *src, value_set ***target_valueset, int *target_valueset_from,bool isgc);
void *variable_value2Page_hc(level *in,l_bucket *src, value_set ***target_valueset, int *target_valueset_from,bool isgc);
#endif
