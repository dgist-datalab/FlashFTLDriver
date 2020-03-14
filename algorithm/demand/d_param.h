/*
 * Demand-based FTL Parameters
 */

#ifndef __DEMAND_PARAM_H__
#define __DEMAND_PARAM_H__

#include "../../include/demand_settings.h"

#ifdef STORE_KEY_FP
#define ENTRY_SIZE (4+(FP_SIZE/8))
#else
#define ENTRY_SIZE (4)
#endif

#define EPP (PAGESIZE / ENTRY_SIZE) // Entry Per Page

#endif
