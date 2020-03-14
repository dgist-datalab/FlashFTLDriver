/*
 * Simple hash table in Demand-based FTL
 */

#ifndef __DEMAND_HASH_TABLE_H__
#define __DEMAND_HASH_TABLE_H__

#include "demand.h"

struct d_hnode {
	lpa_t item;
	struct d_hnode *next;
};
struct d_htable {
	struct d_hnode *bucket;
	int max;
};

struct d_htable *d_htable_init(int max);
void d_htable_free(struct d_htable *ht);
int d_htable_insert(struct d_htable *ht, ppa_t ppa, lpa_t lpa);
int d_htable_find(struct d_htable *ht, ppa_t ppa, lpa_t lpa);

#endif
