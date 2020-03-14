/*
 * Header for find.c
 */

#ifndef __FINE_H__
#define __FINE_H__

#include "demand.h"

int fg_create(cache_t, struct demand_cache *);
int fg_destroy();
int fg_load(lpa_t, request *const, snode *wb_entry);
int fg_list_up(lpa_t, request *const, snode *wb_entry);
int fg_wait_if_flying(lpa_t, request *const, snode *wb_entry);
int fg_touch(lpa_t);
int fg_update(lpa_t, struct pt_struct pte);
bool fg_is_hit(lpa_t);
bool fg_is_full();
struct pt_struct fg_get_pte(lpa_t lpa);
struct cmt_struct *fg_get_cmt(lpa_t lpa);

#endif
