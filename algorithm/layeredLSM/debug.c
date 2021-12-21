#include "./debug.h"
uint32_t print_lba_from_piece_ppa(blockmanager *bm, uint32_t piece_ppa){
	return ((uint32_t*)bm->get_oob(bm, piece_ppa/L2PGAP))[piece_ppa%L2PGAP];
}
