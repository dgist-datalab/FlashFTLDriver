#include "./lsmtree.h"

uint32_t lsmtree_dump(lsmtree *lsm, FILE *fp){
    fwrite(&lsm->param, sizeof(lsmtree_parameter), 1, fp);
    return 1;
}

lsmtree* lsmtree_load(FILE *fp, blockmanager *sm){
    return NULL;
}