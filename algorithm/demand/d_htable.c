#include "d_htable.h"
#include <stdlib.h>

struct d_htable *d_htable_init(int max) {
	struct d_htable *ht = (struct d_htable *)malloc(sizeof(struct d_htable));
	ht->bucket = (struct d_hnode *)calloc(max, sizeof(struct d_hnode));
	ht->max = max;
	return ht;
}

void d_htable_free(struct d_htable *ht) {
	for (int i = 0; i < ht->max; i++) {
		struct d_hnode *hn = &ht->bucket[i];
		struct d_hnode *next = hn->next;

		while (next != NULL) {
			hn = next;
			next = hn->next;
			free(hn);
		}
	}
	free(ht->bucket);
	free(ht);
}

int d_htable_insert(struct d_htable *ht, ppa_t ppa, lpa_t lpa) {
	struct d_hnode *hn = &ht->bucket[ppa%ht->max];
	while (hn->next != NULL) {
		hn = hn->next;
	}
	hn->next = (struct d_hnode *)malloc(sizeof(struct d_hnode));
	hn->next->item = lpa;
	hn->next->next = NULL;
	return 0;
}

int d_htable_find(struct d_htable *ht, ppa_t ppa, lpa_t lpa) {
	struct d_hnode *hn = &ht->bucket[ppa%ht->max];
	while (hn->next != NULL) {
		if (hn->next->item == lpa) {
			return 1;
		}
		hn = hn->next;
	}
	return 0;
}

