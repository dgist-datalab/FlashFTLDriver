#include "compaction.h"
#include "level.h"
#include "lsmtree_scheduling.h"
#include "lsmtree.h"
#include "../../include/sem_lock.h"
#include "../../include/data_struct/list.h"
extern KEYT key_min,key_max;
extern lsmtree LSM;
uint32_t multiple_leveling(int from, int to){
	if(to-from==1){
		compaction_selector(LSM.disk[from],LSM.disk[to],NULL,&LSM.level_lock[to]);
		return 1;
	}

	list *temp_list=list_init();
	//LSM.lop->print_level_summary();
	LSM.delayed_header_trim=true;
	//int lev_number=to-from;
	//int ln_p_from=lev_number+from;
	int origin_from=from;
	KEYT start=key_max,end=key_min;
	level *target_lev;
	level *t_org=LSM.disk[to];
	target_lev=lsm_level_resizing(t_org,NULL);
	LSM.c_level=target_lev;

	/*find min value and max value*/
	for(int i=from; i<t_org->idx; i++){
		if(KEYCMP(start,LSM.disk[i]->start)>0){
			start=LSM.disk[i]->start;
		}
	}
	
	/*move unmatch run*/
	run_t **target_s=NULL;
	uint32_t max_nc_min=LSM.lop->unmatch_find(t_org,start,end,&target_s);
	for(int i=0; target_s[i]!=NULL; i++){
		LSM.lop->insert(target_lev,target_s[i]);
		target_s[i]->iscompactioning=SEQCOMP;
	}
	free(target_s);

	skiplist *body;
	if(from<LSM.LEVELCACHING && from>=0){
		body=LSM.lop->cache_get_body(LSM.disk[from]);
		from++;
	}
	else{
		body=skiplist_init();
	}

	fdriver_lock_t **wait,**read_wait;
	run_t **read_bunch_data, **bunch_data;
	int idx,read_idx; //insert all run except last
	for(int i=from; i<to; i++){
		level *lev=LSM.disk[i];
		lev_iter *iter=LSM.lop->get_iter(lev,lev->start,lev->end);
		wait=(fdriver_lock_t**)malloc(sizeof(fdriver_lock_t*)*lev->n_num*2);
		read_wait=(fdriver_lock_t**)malloc(sizeof(fdriver_lock_t*)*lev->n_num*2);
		read_bunch_data=(run_t**)malloc(sizeof(run_t*)*lev->n_num*2);
		bunch_data=(run_t**)malloc(sizeof(run_t*)*lev->n_num*2);
		idx=0; read_idx=0;

		run_t *now;
		while((now=LSM.lop->iter_nxt(iter))){
			bunch_data[idx]=now;
			wait[idx]=(fdriver_lock_t*)malloc(sizeof(fdriver_lock_t));
			if(htable_read_preproc(now)){
				fdriver_lock_init(wait[idx++],1);
			}
			else{
				read_bunch_data[read_idx]=now;
				fdriver_lock_init(wait[idx],0);
				read_wait[read_idx++]=wait[idx];
				idx++;
			}
		}
		bunch_data[idx]=NULL;
		read_bunch_data[read_idx]=NULL;
		compaction_bg_htable_bulkread(read_bunch_data,read_wait);
		
		fdriver_lock_t *target;
		for(int j=0; j<idx; j++){
			now=bunch_data[j];
			target=wait[j];
			fdriver_lock(target);
			fdriver_destroy(target);
			free(target);

			LSM.lop->normal_merger(body,now,true);//true for wP
			list_insert(temp_list,now);
			//htable_read_postproc(now);
		}
		free(bunch_data);
		free(wait);
	}

	//last level merger
	idx=0; read_idx=0;
	run_t *org_start_r=LSM.lop->get_run_idx(LSM.disk[to],max_nc_min);
	run_t *org_end_r=LSM.lop->get_run_idx(LSM.disk[to],LSM.disk[to]->n_num);
	lev_iter *iter=LSM.lop->get_iter_from_run(LSM.disk[to],org_start_r,org_end_r);
	wait=(fdriver_lock_t**)calloc(sizeof(fdriver_lock_t*),LOWQDEPTH*2+1);//callof for malloc
	bunch_data=(run_t**)malloc(sizeof(run_t*)*(LOWQDEPTH*2+1));	
	bool last_flag=false;
	run_t *result;
	while(!last_flag){
		run_t *now;
		read_wait=(fdriver_lock_t**)malloc(sizeof(fdriver_lock_t*)*(LOWQDEPTH*2+1));
		read_bunch_data=(run_t**)malloc(sizeof(run_t*)*(LOWQDEPTH*2+1));
		for(int i=0; i<2*LOWQDEPTH && (now=LSM.lop->iter_nxt(iter)); i++){
			bunch_data[idx]=now;
			if(!wait[idx]) wait[idx]=(fdriver_lock_t*)malloc(sizeof(fdriver_lock_t));
			if(htable_read_preproc(now)){
				fdriver_lock_init(wait[idx++],1);
			}else{
				read_bunch_data[read_idx]=now;
				fdriver_lock_init(wait[idx],0);
				read_wait[read_idx++]=wait[idx];
				idx++;
			}
		}

		bunch_data[idx]=NULL;
		read_bunch_data[read_idx]=NULL;
		compaction_bg_htable_bulkread(read_bunch_data,read_wait);
		if(!now) last_flag=true;
	
		fdriver_lock_t *target;
		run_t* container[2]={0,};
		for(int j=0; j<idx; j++){
			now=bunch_data[j];
			target=wait[j];
			fdriver_lock(target);
			fdriver_destroy(target);
		//	free(target);
			
			container[0]=now;
			result=LSM.lop->partial_merger_cutter(body,NULL,container,target_lev->fpr);
			compaction_htable_write_insert(target_lev,result,true);
			list_insert(temp_list,now);
//			htable_read_postproc(now);
			free(result);
		}
		idx=0; read_idx=0;
	}
	
	
	while(1){	
		result=LSM.lop->partial_merger_cutter(body,NULL,NULL,target_lev->fpr);

		if(result==NULL) break;

		compaction_htable_write_insert(target_lev,result,true);
		free(result);
	}
	free(bunch_data);
	
	li_node *li;
	for_each_list_node(temp_list,li){
		htable_read_postproc((run_t*)li->data);
	}
	list_free(temp_list);

	for(int i=0;i<LOWQDEPTH*2+1; i++){
		free(wait[i]);
	}
	free(wait);

	
	if(origin_from>=LSM.LEVELCACHING){
		skiplist_free(body);
	}

	//change level
	for(int i=origin_from; i<to; i++){
		level **src_ptr;
		level *src=LSM.disk[i];

		LSM.lop->move_heap(target_lev,src);//move heap

		pthread_mutex_lock(&LSM.level_lock[i]);
		src_ptr=&LSM.disk[i];
		(*src_ptr)=LSM.lop->init(src->m_num, src->idx, src->fpr, src->istier);
		pthread_mutex_unlock(&LSM.level_lock[i]);
		LSM.lop->release(src);
	}
	level **des_ptr;
	pthread_mutex_lock(&LSM.level_lock[to]);
	des_ptr=&LSM.disk[to];
	(*des_ptr)=target_lev;
	LSM.lop->release(t_org);
	pthread_mutex_unlock(&LSM.level_lock[to]);

	LSM.c_level=NULL;
	return 1;
}
