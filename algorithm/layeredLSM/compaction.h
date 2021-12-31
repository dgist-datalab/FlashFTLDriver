#ifndef _COMPACTION_H_
#define _COMPACTION_H_
#include "./lsmtree.h"
void compaction_flush(lsmtree *lsm, run *r);
#endif
