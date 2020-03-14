/*
 * Header for Utilities
 */

#ifndef __DEMAND_UTILITY_H__
#define __DEMAND_UTILITY_H__

#include "demand.h"
#include "page.h"
#include "d_type.h"

/* Defines */
#define QUADRATIC_PROBING(h,c) ((h)+(c)+(c)*(c))
#define LINEAR_PROBING(h,c) (h+c)

#define PROBING_FUNC(h,c) QUADRATIC_PROBING(h,c)
//#define PROBING_FUNC(h,c) LINEAR_PROBING(h,c)

#define WB_HIT(x) ((x) != NULL)
#define CACHE_HIT(x) ((x) != NULL)
#define IS_READ(x) ((x) != NULL)
#define IS_INFLIGHT(x) ((x) != NULL)
#define IS_INITIAL_PPA(x) ((x) == UINT32_MAX)

#define IDX(x) ((x) / EPP)
#define OFFSET(x) ((x) % EPP)

#define PPA_TO_PGA(_ppa_, _offset_) ( ((_ppa_) * GRAIN_PER_PAGE) + (_offset_) )
#define G_IDX(x) ((x) / GRAIN_PER_PAGE)
#define G_OFFSET(x) ((x) % GRAIN_PER_PAGE)

/* Functions */
struct algo_req *make_algo_req_default(uint8_t type, value_set *value);
struct algo_req *make_algo_req_rw(uint8_t type, value_set *value, request *req, snode *wb_entry);
struct algo_req *make_algo_req_sync(uint8_t type, value_set *value);
void free_algo_req(struct algo_req *a_req);

#ifdef HASH_KVSSD
//void copy_key_from_value(KEYT *dst, value_set *src);
void copy_key_from_value(KEYT *dst, value_set *src, int offset);
void copy_key_from_key(KEYT *dst, KEYT *src);
void copy_value(value_set *dst, value_set *src, int size);
void copy_value_onlykey(value_set *dst, value_set *src);
#ifdef DVALUE
//void copy_key_from_grain(KEYT *dst, value_set *src, int offset);
#endif
#endif

lpa_t get_lpa(KEYT key, void *_h_params);

lpa_t *get_oob(blockmanager *bm, ppa_t ppa);
void set_oob(blockmanager *bm, lpa_t lpa, ppa_t ppa, page_t type);
void set_oob_bulk(blockmanager *bm, lpa_t *lpa_list, ppa_t ppa);

struct inflight_params *get_iparams(request *const req, snode *wb_entry);
void free_iparams(request *const req, snode *wb_entry);

int hash_collision_logging(int cnt, rw_t type);

void warn_notfound(char *, int);

int wb_lpa_compare(const void *, const void *);

void insert_retry_read(request *const);
#endif
