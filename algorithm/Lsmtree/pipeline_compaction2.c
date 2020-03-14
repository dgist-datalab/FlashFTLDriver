#include "lsmtree.h"
#include "compaction.h"
#include "skiplist.h"
#include "page.h"
#include "bloomfilter.h"
#include "nocpy.h"
#include "lsmtree_scheduling.h"
extern lsmtree LSM;
pl_run *make_pl_run_array(level *t, uint32_t *num){
	//first lock
	*num=LSM.lop->get_number_runs(t);
	pl_run *res=(pl_run*)malloc(sizeof(pl_run)*(*num));
	/*if(t->idx<LSM.LEVELCACHING){
		run_t **tr;
		for(int i=0; tr[i]!=NULL; i++){
			res[i].r=tr[i];
			res[i].lock=(fdriver_lock_t*)malloc(sizeof(fdriver_lock_t));
			fdriver_mutex_init(res[i].lock);
		}
	}else{*/
		lev_iter* iter=LSM.lop->get_iter(t,t->start,t->end);
		run_t *now;
		int i=0;
		while((now=LSM.lop->iter_nxt(iter))){
			res[i].r=now;
			res[i].lock=(fdriver_lock_t*)malloc(sizeof(fdriver_lock_t));
			fdriver_mutex_init(res[i].lock);
			i++;
		}
//	}
	return res;
}

void pl_run_free(pl_run *pr, uint32_t num ){
	for(int i=0; i<num; i++){
		fdriver_destroy(pr[i].lock);
		free(pr[i].lock);
		htable_read_postproc(pr[i].r);
	}
	free(pr);
}

void *level_insert_write(level *t, run_t *data){
	compaction_htable_write_insert(t,data,false);
	free(data);
	return NULL;
}

uint32_t pipe_partial_leveling(level *t, level *origin, leveling_node* lnode, level *upper){
	compaction_sub_pre();

	uint32_t u_num=0, l_num=0;
	pl_run *u_data=NULL, *l_data=NULL;
	if(upper){
		u_data=make_pl_run_array(upper,&u_num);
	}
	l_data=make_pl_run_array(origin,&l_num);

	uint32_t all_num=u_num+l_num+1;
	run_t **read_target=(run_t **)malloc(sizeof(run_t*)*all_num);
	fdriver_lock_t **lock_target=(fdriver_lock_t**)malloc(sizeof(fdriver_lock_t*)*all_num);

	int cnt=0;
	uint32_t min_num=u_num<l_num?u_num:l_num;
	for(int i=0; i<min_num; i++){
		run_t *t; fdriver_lock_t *tl;
		for(int j=0; j<2; j++){
			t=!j?u_data[i].r:l_data[i].r;
			tl=!j?u_data[i].lock:l_data[i].lock;
			if((!j && upper && upper->idx<LSM.LEVELCACHING) || htable_read_preproc(t)){
				continue;
			}
			else{
				fdriver_lock(tl);
				read_target[cnt]=t;
				lock_target[cnt]=tl;
				cnt++;
			}
		}
	}
	
	for(int i=min_num; i<u_num; i++){
		if((upper && upper->idx<LSM.LEVELCACHING) || htable_read_preproc(u_data[i].r)){
			continue;
		}
		else{
			fdriver_lock(u_data[i].lock);
			read_target[cnt]=u_data[i].r;
			lock_target[cnt]=u_data[i].lock;
			cnt++;
		}
	}

	for(int i=min_num; i<l_num; i++){
		if(htable_read_preproc(l_data[i].r)){
			continue;
		}
		else{
			fdriver_lock(l_data[i].lock);
			read_target[cnt]=l_data[i].r;
			lock_target[cnt]=l_data[i].lock;
			cnt++;
		}
	}
	
	read_target[cnt]=NULL;
	compaction_bg_htable_bulkread(read_target,lock_target);

	LSM.lop->partial_merger_cutter(lnode?lnode->mem:NULL,u_data,l_data,u_num,l_num,t,level_insert_write);

	compaction_sub_post();
	if(u_data) pl_run_free(u_data,u_num);
	pl_run_free(l_data,l_num);
	return 1;
}	
