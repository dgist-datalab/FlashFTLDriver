#ifndef __H_SETTING__
#define __H_SETTING__
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#include<stdint.h>
#include <stdlib.h>
#include<stdio.h>
#include <string.h>
#include <execinfo.h>

#define K (1024)
#define M (1024*K)
#define G (1024*M)
#define T (1024L*G)
#define P (1024L*T)
#define MILI (1000000)

#define GIGAUNIT 64L
#ifndef OP
#define OP 85
#endif
#define SHOWINGSIZE (GIGAUNIT * G)
#define TOTALSIZE (SHOWINGSIZE + (SHOWINGSIZE/100*(100-OP)))
#define REALSIZE (512L*G)
#define PAGE_TARGET_KILO (16)
#define PAGESIZE (PAGE_TARGET_KILO*K)
#define _PPB (256*8/PAGE_TARGET_KILO)

#if 1
	#define CARD_NUM 2
	#define BUS_NUM 8
	#define CHIP_NUM 8
#else // using BPS=1
	#define CARD_NUM 1
	#define BUS_NUM 1
	#define CHIP_NUM 1
#endif

#define BPS (BUS_NUM *CHIP_NUM*CARD_NUM)
#define _PPS (_PPB*BPS)

#define PUNIT (BPS)

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)<(b)?(b):(a))
#define ABS(a) ((a)<0?-(a):(a))
#define CEIL(a,b) ((a)/(b) + ((a)%(b)?1:0))

#define OOB_SIZE 128
#define LPAGESIZE 4096
#define L2PGAP (PAGESIZE/LPAGESIZE)
#define BLOCKSIZE (_PPB*PAGESIZE)
#define _NOP (TOTALSIZE/PAGESIZE)
#define _NOS (TOTALSIZE/(_PPS*PAGESIZE))
#define _NOB (BPS*_NOS)
#define _RNOS (REALSIZE/(_PPS*PAGESIZE))//real number of segment

#define RANGE (SHOWINGSIZE/LPAGESIZE)
#define DEVFULL ((uint32_t)TOTALSIZE/LPAGESIZE)
#define TOTALLPN ((uint32_t)RANGE)

#define FSTYPE uint8_t
#define ppa_t uint32_t
#define KEYT uint32_t
#define BLOCKT uint32_t
#define PTR_BIT 32

#define QSIZE		(128) 
#define LOWQDEPTH	(128)
#define QDEPTH		(128)
#define DEV_QDEPTH (128)

#define THPOOL
#define NUM_THREAD 4

#ifndef __GNUG__
typedef enum{false,true} bool;
#endif

#endif
