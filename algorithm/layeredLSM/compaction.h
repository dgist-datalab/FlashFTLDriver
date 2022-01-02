#ifndef _COMPACTION_H_
#define _COMPACTION_H_
#include "./lsmtree.h"
void compaction_thread_run(void *arg, int idx);
void compaction_flush(lsmtree *lsm, run *r);
void compaction_clean_last_level(lsmtree *lsm);
#endif
