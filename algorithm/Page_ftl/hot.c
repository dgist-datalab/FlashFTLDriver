#include "hot.h"
#include "model.h"
#include "map.h"
#include "midas.h"

extern algorithm page_ftl;
extern STAT* midas_stat;
extern mini_model *mmodel;
extern HF_Q* hot_q; 

void hf_init(HF **hotf) {
	(*hotf) = (HF*)malloc(sizeof(HF));
	(*hotf)->max_val = 3;
	(*hotf)->hot_val = 3;
	(*hotf)->tw_ratio = 1.0;

	(*hotf)->make_flag=1;
	(*hotf)->use_flag=1;

	(*hotf)->tw = (long)LBANUM;
	(*hotf)->left_tw = (*hotf)->tw;
	(*hotf)->cold_tw = (long)LBANUM;

	(*hotf)->G0_vr_sum=0.0;
	(*hotf)->G0_vr_num=0.0;
	(*hotf)->seg_age=0.0;
	(*hotf)->seg_num=0.0;
	(*hotf)->avg_seg_age=0.0;

	(*hotf)->G0_traffic_ratio=0.0;
	(*hotf)->tot_traffic=0.0;
	(*hotf)->G0_traffic=0.0;

	(*hotf)->hot_lba_num=0;

	(*hotf)->err_cnt=0;
	(*hotf)->tmp_err_cnt=0;

	(*hotf)->cur_hf = (int*)malloc(sizeof(int)*LBANUM);
	memset((*hotf)->cur_hf, 0, sizeof(int)*LBANUM);

	hf_q_init();
}

void hf_q_init() {
	hot_q = (HF_Q *)malloc(sizeof(HF_Q));
	
	hot_q->queue_idx=0;

	hot_q->g0_traffic=0.0;
	hot_q->g0_size=0.0;
	hot_q->g0_valid=0.0;
	hot_q->queue_max=10;

	hot_q->extra_size=0;
	hot_q->extra_traffic=0.0;

	hot_q->calc_traffic=0.0;
	hot_q->calc_size=0;
	hot_q->calc_unit=0;

	hot_q->best_extra_size=0;
	hot_q->best_extra_traffic=0.0;
	hot_q->best_extra_unit=0.0;

	hot_q->g0_traffic_queue=(double*)malloc(sizeof(double)*hot_q->queue_max);
	hot_q->g0_size_queue=(double*)malloc(sizeof(double)*hot_q->queue_max);
	hot_q->g0_valid_queue=(double*)malloc(sizeof(double)*hot_q->queue_max);

	memset(hot_q->g0_traffic_queue, 0, sizeof(double)*hot_q->queue_max);
	memset(hot_q->g0_size_queue, 0, sizeof(double)*hot_q->queue_max);
	memset(hot_q->g0_valid_queue, 0, sizeof(double)*hot_q->queue_max);
}

void hf_q_reset(bool true_reset) {
	hot_q->g0_traffic=0;
	hot_q->g0_size=0;
	hot_q->g0_valid=0.0;
	hot_q->queue_idx=0;

	hot_q->extra_size=0;
	hot_q->extra_traffic=0.0;
	
	hot_q->best_extra_size=0;
	hot_q->best_extra_traffic=0.0;
	hot_q->best_extra_unit=0.0;
	
	hot_q->calc_traffic=0.0;
	hot_q->calc_size=0;
	hot_q->calc_unit=0;

	if (true_reset) {
		memset(hot_q->g0_traffic_queue, 0, sizeof(double)*hot_q->queue_max);
		memset(hot_q->g0_size_queue, 0, sizeof(double)*hot_q->queue_max);
		memset(hot_q->g0_valid_queue, 0, sizeof(double)*hot_q->queue_max);
	}
}

void hf_q_calculate() {
	hf_q_reset(false);

	double hfq_cnt=0.0;
	for (int i=0;i<hot_q->queue_max; i++) {
		if (hot_q->g0_traffic_queue[i] != 0.0) hfq_cnt++;
		hot_q->g0_traffic += hot_q->g0_traffic_queue[i];
		hot_q->g0_size += hot_q->g0_size_queue[i];
		hot_q->g0_valid += hot_q->g0_valid_queue[i];
	}
	if (hfq_cnt == 0.0) {
		printf("there is no information about hotfilter!!\n");
		abort();
	}
	hot_q->g0_traffic = hot_q->g0_traffic/hfq_cnt;
	hot_q->g0_size = hot_q->g0_size/hfq_cnt;
	hot_q->g0_valid = hot_q->g0_valid/hfq_cnt;

	hot_q->g0_size++; //active segment
	hot_q->g0_size = floor(hot_q->g0_size+0.5);
	
	if ((hot_q->g0_valid > 0.2) || (hot_q->g0_traffic < 0.15)) {
		printf("hotfilter accuracy is low, so FIXED\n");
		hot_q->is_fix=true;
	}else hot_q->is_fix=false;
	
	printf("==============HOT FILTER INFO=============\n");
	printf("- avg. traffic: %.3f\n", hot_q->g0_traffic);
	printf("- avg. size   : %.3f\n", hot_q->g0_size);
	printf("- avg. valid  : %.3f\n", hot_q->g0_valid);
	printf("------------------------------------------\n");

	return;
}

void hf_destroy(HF *hotf) {
	free(hotf->cur_hf);
	free(hotf);
}

void hf_metadata_reset(HF* hotf) {
	hotf->make_flag=0;
	hotf->use_flag=0;
	hotf->G0_vr_num=0;
	hotf->G0_vr_sum=0;
	hotf->seg_age=0.0;
	hotf->seg_num=0.0;
	hotf->avg_seg_age=0.0;
	hotf->G0_traffic_ratio=0.0;
	hotf->tot_traffic=0.0;
	hotf->G0_traffic=0.0;
	hotf->hot_lba_num=0;
	hotf->left_tw = hotf->tw;
	hotf->err_cnt=0;
	hotf->tmp_err_cnt=0;
}

void hot_merge() {
	pm_body *p = (pm_body*)page_ftl.algo_body;

	//0: move the whole queue to the heap
	int size = page_ftl.bm->jy_move_q2h(page_ftl.bm, p->group[0], 0);
	if ((size+1) != midas_stat->g->gsize[0]) {
		printf("size miss: stat->gsize and real queue size is different\n");
		printf("in hot merge function\n");
		abort();
	}
	q_free(p->group[0]);
	p->group[0] = NULL;
	//TODO p->m->config[i]?
}

void hf_reset(int flag, HF* hotf) {
	memset(hotf->cur_hf, 0, sizeof(int)*LBANUM);
	hotf->left_tw = hotf->tw;
	if (flag==1) hotf->make_flag=1;
	hotf->use_flag=0;
}

void hf_update_model(double traffic, HF *hotf) {
	double tmp;
	if (traffic==0.0) return;
	hot_q->g0_traffic_queue[hot_q->queue_idx] = traffic;
	hot_q->g0_size_queue[hot_q->queue_idx] = midas_stat->g->gsize[0];
	hot_q->g0_valid_queue[hot_q->queue_idx] = hotf->G0_vr_sum/hotf->G0_vr_num;
	hot_q->queue_idx++;
	if (hot_q->queue_idx == hot_q->queue_max) hot_q->queue_idx = 0;
	
	return;
}

int hf_cnt=0;
int print_cond=1;
void hf_update(HF* hotf) {
	hf_cnt++;
	if (hotf->left_tw < 0) {
		if (hf_cnt==print_cond) printf("wait more time window for hotfilter: %ld\n", -hotf->left_tw);
		hotf->left_tw=0;
	}
	if (hf_cnt==print_cond) printf("[HF-NOTICE] HOT LBA NUM: %d (%.2f%%)\n", 
			hotf->hot_lba_num, (double)hotf->hot_lba_num/(double)LBANUM*100.0);
	double prev_seg_age=0.0;
	double tr=0.0;
	tr = hotf->G0_traffic/hotf->tot_traffic;
	if (hf_cnt==print_cond) printf("[HF-NOTICE] HOT FILTER traffic: %.3f%% (%d / %d)\n",
			tr*100, (int)hotf->G0_traffic, (int)hotf->tot_traffic);
	hotf->G0_traffic_ratio=tr;

	if (hotf->G0_traffic != 0) hf_update_model(tr, hotf);
	prev_seg_age = hotf->tw/hotf->tw_ratio;

	double avg_g0 = 0.0;
	hotf->avg_seg_age=(double)hotf->seg_age/(double)hotf->seg_num;
	avg_g0 = hotf->avg_seg_age/(double)_PPS/L2PGAP;

	if (hf_cnt==print_cond) printf("[HF-NOTICE] Calculated avg G0: %.3f%% (num of victim: %d)\n", avg_g0, (int)hotf->seg_num);
	if (prev_seg_age != 0.0) {
		if (hotf->avg_seg_age >= prev_seg_age*2) {
			hotf->avg_seg_age = prev_seg_age*1.5;
		} else {
			hotf->avg_seg_age = (hotf->avg_seg_age+prev_seg_age)/2.0;
		}
	}
	avg_g0 = hotf->avg_seg_age/(double)_PPS/L2PGAP;
	long tw = (long)(hotf->avg_seg_age*hotf->tw_ratio);
	if (hf_cnt==print_cond) printf("[HF-NOTICE] NEW HOT FILTER, age: %.3f%% (%ld)\n", avg_g0, tw);
	if (hf_cnt==print_cond) hf_cnt=0;

	hotf->tw=tw;
	hotf->left_tw=tw-1;
	hotf->seg_age=0.0;
	hotf->seg_num=0.0;
	hotf->G0_traffic=0.0;
	hotf->tot_traffic=0.0;
	hotf->G0_vr_sum=0.0;
	hotf->G0_vr_num=0.0;

	return;
}

void hf_generate(uint32_t lba, int gnum, HF* hotf, int hflag) {
	if (hotf->make_flag==0) return;
	pm_body *p=(pm_body*)page_ftl.algo_body;

	if ((hotf->make_flag==1) && (hotf->left_tw<=0) && (hotf->seg_num>0)) hf_update(hotf);

	
	if (hotf->cold_tw>0) {
		//printf("[HF-NOTICE] COLD start end\n");
		hotf->cold_tw--;
		return;
	}
	

	if (hflag) {
		//update the LBA to the hotfilter
		hotf->left_tw--;
		if (gnum==1) {
			double seg_age=page_ftl.bm->jy_get_timestamp(page_ftl.bm, p->mapping[lba]/_PPS/L2PGAP);
			/*
			if (seg_age == 0) {
				//it can be 0, because get_ppa, gc G0 -> G1, and accessed lba to the G1
				//printf("why the segment stamp is 0??\n");
				printf("lba: %u\n", lba);
				//abort();
			}
			*/
			double tmp_age=hotf->tw/hotf->tw_ratio;
			//printf("prev age: %.3f, new age: %.3f\n", tmp_age, seg_age);
			if (seg_age <= tmp_age) {
				//hot lba
				if (hotf->cur_hf[lba] <= hotf->max_val-1) {
					hotf->cur_hf[lba]++;
					if (hotf->cur_hf[lba] == hotf->hot_val) hotf->hot_lba_num++;
				}
			} else {
				//not hot lba
				if (hotf->cur_hf[lba]>0) {
					hotf->cur_hf[lba]--;
					if (hotf->cur_hf[lba] == (hotf->hot_val-1)) hotf->hot_lba_num--;
				}
			}
		}
	} else {
		if (hotf->cur_hf[lba]>0) {
			hotf->cur_hf[lba]--;
			if (hotf->cur_hf[lba] == hotf->hot_val-1) hotf->hot_lba_num--;
			uint32_t gnum = seg_get_ginfo(p->mapping[lba]/_PPS);
			if (gnum == 0) hotf->cur_hf[lba]--;
		}
	}	
	return;
}

int hf_check(uint32_t lba, HF* hotf) {
	if (hotf->use_flag == 0) return 1;
	else {
		hotf->tot_traffic++;
		if (hotf->cur_hf[lba] >= hotf->hot_val) {
			hotf->G0_traffic++;
			return 0;
		} else return 1;

	}
}

