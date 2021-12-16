#ifndef _COMPACTION_H_
#define _COMPACTION_H_
#include "./shortcut.h"
#include "./run.h"
run* compaction_test(sc_master *sc, uint32_t merge_num, uint32_t map_type,
		float fpr, L2P_bm *bm);
#endif
