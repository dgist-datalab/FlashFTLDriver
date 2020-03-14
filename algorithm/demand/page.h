/*
 * Header for Page Allocation
 */

#ifndef __DEMAND_PAGE_H__
#define __DEMAND_PAGE_H__

/* Defines */
typedef enum {
	DATA, MAP,
} page_t;

/* Structures */
struct gc_table_struct {
	value_set *origin;
	lpa_t *lpa_list;
	ppa_t ppa;
};

/* page.c */
int page_create(blockmanager *bm);
ppa_t get_dpage(blockmanager *bm);
ppa_t get_tpage(blockmanager *bm);
int dpage_gc(blockmanager *bm);
int tpage_gc(blockmanager *bm);

int validate_page(blockmanager *bm, ppa_t ppa, page_t type);
int invalidate_page(blockmanager *bm, ppa_t ppa, page_t type);

/* dvalue_gc.c */
#ifdef DVALUE
int dpage_gc_dvalue(blockmanager *bm);
#endif

#endif
