#include "../../include/container.h"


typedef struct HotFilter {
	int *cur_hf;
	int max_val;
	int hot_val;
	double tw_ratio;
	int make_flag;
	int use_flag;
	
	long tw;
	long left_tw;
	long cold_tw;

	double G0_vr_sum;
	double G0_vr_num;

	double seg_age;
	double seg_num;
	double avg_seg_age;

	double G0_traffic_ratio;
	double tot_traffic;
	double G0_traffic;

	int hot_lba_num;

	int err_cnt;
	int tmp_err_cnt;
}HF;

typedef struct HotFilter_Q{
	double *g0_traffic_queue;
	double *g0_size_queue;
	double *g0_valid_queue;
	int queue_idx;

	double g0_traffic;
	double g0_size;
	double g0_valid;
	int queue_max;

	bool is_fix;

	int extra_size;
	double extra_traffic;

	double calc_traffic;
	int calc_size;
	int calc_unit;

	int best_extra_size;
	double best_extra_traffic;
	double best_extra_unit;
}HF_Q;

void hf_init(HF**hotf);
void hf_q_init();
void hf_q_reset(bool true_reset);
void hf_q_calculate();
void hf_destroy(HF* hotf);
void hf_metadata_reset(HF* hotf);
void hot_merge();
void hf_reset(int flag, HF* hotf);
void hf_update_model(double traffic, HF* hotf);
void hf_update(HF* hotf);
void hf_generate(uint32_t lba, int gnum, HF* hotf, int hflag);
int hf_check(uint32_t lba, HF* hotf);
