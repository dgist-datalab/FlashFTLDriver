#ifndef __CHEEZE_INF_H__
#define __CHEEZE_INF_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "../include/container.h"
enum req_opf {
    /* read sectors from the device */
    REQ_OP_READ     = 0,
    /* write sectors to the device */
    REQ_OP_WRITE        = 1,
    /* flush the volatile write cache */
    REQ_OP_FLUSH        = 2,
    /* discard sectors */
    REQ_OP_DISCARD      = 3,
    /* get zone information */
    REQ_OP_ZONE_REPORT  = 4,
    /* securely erase sectors */
    REQ_OP_SECURE_ERASE = 5,
    /* seset a zone write pointer */
    REQ_OP_ZONE_RESET   = 6,
    /* write the same sector many times */
    REQ_OP_WRITE_SAME   = 7,
    /* write the zero filled sector many times */
    REQ_OP_WRITE_ZEROES = 9,

    /* SCSI passthrough using struct scsi_request */
    REQ_OP_SCSI_IN      = 32, 
    REQ_OP_SCSI_OUT     = 33, 
    /* Driver private requests */
    REQ_OP_DRV_IN       = 34, 
    REQ_OP_DRV_OUT      = 35, 

    REQ_OP_LAST,
};


struct cheeze_req_user {
    int id; 
    int op; 
    char *buf;
    unsigned int pos; // sector_t but divided by 4096
    unsigned int len;
} __attribute__((aligned(8), packed));

typedef struct cheeze_req_user cheeze_req;

#define COPY_TARGET "/tmp/vdb"

void init_cheeze();
void free_cheeze();
vec_request *get_vectored_request();

#endif
