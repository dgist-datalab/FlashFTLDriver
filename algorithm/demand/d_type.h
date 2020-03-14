/*
 * Demand-based FTL Type definitions with including basic headers
 */

#ifndef __DEMAND_TYPE_H__
#define __DEMAND_TYPE_H__

#include "../../include/settings.h"
#include "../../include/demand_settings.h"
#include "../../include/types.h"

typedef uint32_t lpa_t;
typedef uint32_t ppa_t;
typedef ppa_t pga_t;

#if (FP_SIZE<=8)
typedef uint8_t fp_t;
#define FP_MAX UINT8_MAX

#elif (FP_SIZE<=16)
typedef uint16_t fp_t;
#define FP_MAX UINT16_MAX

#elif (FP_SIZE<=32) 
typedef uint32_t fp_t;
#define FP_MAX UINT32_MAX

#elif (FP_SIZE<=64) 
typedef uint64_t fp_t;
#define FP_MAX UINT64_MAX
#endif


typedef enum {
	READ, WRITE
} rw_t;

typedef enum {
	HASH_KEY_INITIAL,
	HASH_KEY_NONE,
	HASH_KEY_SAME,
	HASH_KEY_DIFF,
} hash_cmp_t;

typedef enum {
	CLEAN, DIRTY
} cmt_state_t;

typedef enum {
	GOTO_LOAD, GOTO_LIST, GOTO_EVICT, 
	GOTO_COMPLETE, GOTO_READ, GOTO_WRITE,
	GOTO_UPDATE,
} jump_t;

typedef enum {
	COARSE_GRAINED,
	FINE_GRAINED,
	PARTED
} cache_t;

#endif
