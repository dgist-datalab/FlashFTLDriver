#ifndef __H_SETLSM__
#define __H_SETLSM__
#include "settings.h"

/*lsmtree structure*/
#define FULLMAPNUM  1024
#ifdef KVSSD
#define KEYBITMAP 1024
#endif

#if LEVELN!=1
#define BLOOM
//#define MONKEY
#endif


//#define EMULATOR

#define DEFKEYINHEADER ((PAGESIZE-KEYBITMAP)/DEFKEYLENGTH)
//#define ONESEGMENT (DEFKEYINHEADER*DEFVALUESIZE)

#define KEYLEN(a) (a.len+sizeof(ppa_t))
#define READCACHE
#define RANGEGETNUM 2
//#define USINGSLAB

#define NOEXTENDPPA(ppa) (ppa/NPCINPAGE)
/*lsmtree flash thread*/
#define KEYSETSIZE 8
#define CTHREAD 1
#define CQSIZE 128
#define FTHREAD 1
#define FQSIZE 2
#define RQSIZE 1024
#define WRITEWAIT
#define MAXKEYSIZE 255

/*compaction*/
#define MAXITER 16
#define SPINLOCK
#endif
