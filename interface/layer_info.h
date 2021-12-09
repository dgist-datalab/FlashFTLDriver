#ifndef __H_LINFO__
#define __H_LINFO__
#include "../include/container.h"
#include "../blockmanager/block_manager_master.h"
#include "threading.h"
//alogrithm layer
extern struct algorithm __normal;
extern struct algorithm __badblock;
extern struct algorithm demand_ftl;
extern struct algorithm page_ftl;
extern struct algorithm algo_lsm;
extern struct algorithm demand_ftl;
extern struct algorithm lsm_ftl;
extern struct algorithm layered_lsm;

//device layer
extern struct lower_info memio_info;
extern struct lower_info aio_info;
extern struct lower_info net_info;
extern struct lower_info my_posix; //posix, posix_memory,posix_async
extern struct lower_info no_info;
extern struct lower_info amf_info;
extern struct lower_info pu_manager;

static void layer_info_mapping(master_processor *mp, bool data_load, int argc, char **argv){
#ifdef PARALLEL_MANAGER
	mp->li=&pu_manager;
#else
#if defined(posix) || defined(posix_async) || defined(posix_memory)
	mp->li=&my_posix;
#elif defined(bdbm_drv)
	mp->li=&memio_info;
#elif defined(network)
	mp->li=&net_info;
#elif defined(linux_aio)
	mp->li=&aio_info;
#elif defined(no_dev)
	mp->li=&no_info;
#elif defined(AMF)
	mp->li=&amf_info;
#endif
#endif


#ifdef normal
	mp->algo=&__normal;
#elif defined(Page_ftl)
	mp->algo=&page_ftl;
#elif defined(DFTL)
	mp->algo=&demand_ftl;
#elif defined(demand)
	mp->algo=&__demand;
#elif defined(lsmftl)
	mp->algo=&lsm_ftl;
#elif defined(layeredLSM)
	mp->algo=&layered_lsm;
#elif defined(badblock)
	mp->algo=&__badblock;
#endif

	if(!data_load){
		mp->li->create(mp->li,mp->bm);
	}

	mp->bm=blockmanager_factory(SEQ_BM, mp->li);


	if(mp->algo->argument_set){
		mp->algo->argument_set(argc,argv);
	}

	if(!data_load){
		mp->algo->create(mp->li, mp->bm, mp->algo);
	}
}
#endif
