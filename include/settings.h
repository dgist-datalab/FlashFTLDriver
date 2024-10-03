#ifndef __H_SETTING__
#define __H_SETTING__
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#include<stdint.h>
#include <stdlib.h>
#include<stdio.h>
#include <string.h>
#include <execinfo.h>

#define _K (1024)
#define _M (1024*_K)
#define _G (1024*_M)
#define _T (1024L*_G)
#define _P (1024L*_T)
#define MILI (1000000)

#define GIGAUNIT 128LL//440LL-->max
#ifndef OP
#define OP 90
#endif
#define SHOWINGSIZE (GIGAUNIT * _G)
#define TOTALSIZE (SHOWINGSIZE + (SHOWINGSIZE/100*(100-OP)))
#define REALSIZE (256L*_G)
#define PAGE_TARGET_KILO (16)
#define PAGESIZE (PAGE_TARGET_KILO*_K)
#define _PPB (256*8/PAGE_TARGET_KILO)
#define COPYMETA_ONLY 10
//#define GLOBAL_WRITE_BUFFER (128*1024)//(256*1024) //1MB

#if 1
	#define CARD_NUM 1
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

#define OOB_SIZE 16
#define LPAGESIZE 4096
#define L2PGAP (PAGESIZE/LPAGESIZE)
#define BLOCKSIZE (_PPB*PAGESIZE)
#define _NOS (TOTALSIZE/(_PPS*PAGESIZE))
#define _NOP ((_NOS)*_PPS)
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

#define QSIZE		(64) 
#define LOWQDEPTH	(64)
#define QDEPTH		(64)
#define DEV_QDEPTH 	(64)

#define THPOOL
#define NUM_THREAD 4

#ifndef __GNUG__
typedef enum{false,true} bool;
#endif

#endif
