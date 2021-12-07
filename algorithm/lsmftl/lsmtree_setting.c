#include "lsmtree.h"
#include "io.h"
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include "function_test.h"
#include "segment_level_manager.h"
#include "./design_knob/design_knob.h"
#include "./design_knob/lsmtree_param_module_double.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
extern lsmtree LSM;
float TARGETFPR;
void lsmtree_param_defualt_setting(){
	/*
	LSM.param.LEVELN=2;
	LSM.param.mapping_num=(SHOWINGSIZE/LPAGESIZE/KP_IN_PAGE/LSM.param.last_size_factor);
	LSM.param.normal_size_factor=get_size_factor(LSM.param.LEVELN, LSM.param.mapping_num);
	LSM.param.last_size_factor=LSM.param.normal_size_factor;
	LSM.param.version_number=LSM.last_size_factor+1;
	LSM.param.version_enable=true;

	LSM.param.leveling_rhp.type=HELPER_BF_PTR_GUARD;
	LSM.param.leveling_rhp.target_prob=LSM.param.read_amplification;
	LSM.param.leveling_rhp.member_num=KP_IN_PAGE;

	LSM.param.tiering_rhp.type=HELPER_PLR;
	LSM.param.tiering_rhp.slop_bit=8;
	LSM.param.tiering_rhp.range=5;
	LSM.param.tiering_rhp.member_num=KP_IN_PAGE;*/
}

static void print_level_param(){
	printf("[param] # of level: %u\n", LSM.param.LEVELN);
	/*print summary*/
	tree_param tr=LSM.param.tr;

	printf("[param] normal size factor: %f\n", LSM.param.normal_size_factor);
	printf("[param] read amplification: %f\n", LSM.param.read_amplification+1);
	printf("[param] version number :%lu\n", tr.run_num);
	printf("[param] write_buffer ent: %u\n", LSM.param.write_buffer_ent);
	printf("[param] write_buffer byte: %u\n", LSM.param.write_buffer_bit/8);
	printf("[param] reclaim ppa target: %u (%u)\n", LSM.param.reclaim_ppa_target, LSM.param.reclaim_ppa_target*L2PGAP);
#ifdef DYNAMIC_HELPER_ASSIGN
	printf("[param] dynamic helper assign border:%.2lf\n", LSM.param.BF_PLR_border);
#endif

	printf("[PERF] WAF:%.2lf+GC\n", tr.WAF);
	printf("[PERF] RAF:%.3lf\n", LSM.param.read_amplification+1);
}

static void print_help(){
	printf("-----help-----\n");
	printf("parameters (L,S,a,b,r,R)\n");
	printf("-L: set total levels of LSM-tree\n");
	printf("-S: set size factor of LSM-tree\n");
	printf("-a: set read amplification (float type)\n");
	//printf("-v: set number of version\n");
	printf("-b: bit num for plr line\n");
	printf("-r: error lange for plr\n");

	printf("-R: set read helper type\n");
	printf("\t%d: %s\n", 0, read_helper_type(0));
	for(int i=0; i<READHELPER_NUM; i++){
		printf("\t%d: %s\n", 1<<i, read_helper_type(1<<i));
	}
}

uint32_t calc_target_bit=48;
uint32_t lsmtree_argument_set(int argc, char *argv[]){
	int c;
	uint64_t target_memory_usage_bit=0;
	uint32_t percentage=21;
	TARGETFPR=0.1;
	while((c=getopt(argc,argv,"mMhHfFb"))!=-1){
		switch(c){
			case 'h':
			case 'H':
				print_help();
				exit(1);
				break;
			case 'm':
			case 'M':
				percentage=atoi(argv[optind]);		
				break;
			case 'f':
			case 'F':
				TARGETFPR=atof(argv[optind]);
				break;
			case 'b':
				calc_target_bit=atoi(argv[optind]);
				break;
			default:
				printf("invalid parameters\n");
				exit(1);
				break;
		}
	}

	uint32_t error=TARGETFPR * 100;
	init_memory_info(error);

	target_memory_usage_bit=(uint64_t)((double)RANGE*48/100*percentage);
	LSM.param=lsmtree_memory_limit_to_setting(target_memory_usage_bit);
/*
	for(uint32_t i=2; i<10; i++){
		double line_per_chunk=0;
		double plr_lpc[20]={0,};
		double memory_usage=lsmtree_memory_limit_of_WAF(i, &line_per_chunk, plr_lpc);
		printf("%u\t%.2f\t%.2f\t%.2lf\n", i, TARGETFPR+1, memory_usage/RANGE/48*100, line_per_chunk, plr_lpc);
		for(uint32_t j=0; j<=i; j++){
			printf("\t%.2lf", plr_lpc[j]);
		}
		printf("\n");
	}
*/
	printf("------------------------------------------\n");
	print_level_param();
	printf("------------------------------------------\n");
	return 1;
}

uint64_t lsmtree_memory_limit_of_WAF(uint32_t WAF, double *avg_line_per_chunk, double *temp_plr_lpc){
	uint64_t res=UINT64_MAX;
	double plr_lpc[20];
	for(uint32_t divide=2; divide<=RANGE*48/2; divide*=2){
		uint32_t write_buffer_memory_bit=RANGE*48/divide;
		uint32_t buffered_ent=write_buffer_memory_bit/(48+48);
		if(buffered_ent==0) break;
		uint32_t chunk_num=RANGE/buffered_ent+(RANGE%buffered_ent?1:0);
		for(uint32_t level=1; level<=WAF; level++){ //level
			uint64_t memory_size=0;
			double lpc=0;
			uint32_t plr_level_cnt=0;
			memset(plr_lpc, 0, sizeof(plr_lpc));
			double size_factor=get_size_factor(level, chunk_num);
			bool last=size_factor==2;

			memory_size+=ceil(log2(size_factor *level)) * RANGE;
			memory_size+=write_buffer_memory_bit;

			for(uint32_t j=1; j<=level; j++){
				uint32_t run_size=buffered_ent * pow(size_factor, j-1);
				uint64_t level_size=0;
				
				if(j==level){
					level_size=buffered_ent * ceil(pow(size_factor, j));
				}
				else{
					level_size=buffered_ent * ceil(pow(size_factor, j))-pow(size_factor, j-1);
				}

				level_size=level_size>RANGE?RANGE:level_size;
				run_size=run_size>RANGE?RANGE:run_size;
				double run_coverage_ratio=(double)run_size/RANGE;
				if(bf_memory_per_ent(run_coverage_ratio) > plr_memory_per_ent(run_coverage_ratio)){
					plr_lpc[j]=get_line_per_chunk(run_coverage_ratio);
					lpc+=get_line_per_chunk(run_coverage_ratio);
					plr_level_cnt++;
				}
				uint64_t run_memory_usage_bit=level_size * MIN(bf_memory_per_ent(run_coverage_ratio),
						plr_memory_per_ent(run_coverage_ratio));

				memory_size+=run_memory_usage_bit;
			}
			lpc/=plr_level_cnt;
			if(memory_size!=0){
				if(res>memory_size){
					res=memory_size;
					*avg_line_per_chunk=lpc;
					memcpy(temp_plr_lpc, plr_lpc, sizeof(double)*20);
			//		printf("\t%u\t%.2lf\t%.2lf\t%f\n", level, size_factor, (double)res/RANGE/48*100, 
			//				(double)write_buffer_memory_bit/RANGE/48*100);
				}
			}
			if(last) break;
		}
	}
	return res;
}

void print_tree_param(tree_param *set, uint32_t number){
	printf("LEVEL\tWAF\tSF\trun_num\tmemory\n");
	for(uint32_t i=0; i<number; i++){
		printf("%u\t%.2lf\t%.2lf\t%lu\t%.3lf\n", 
				i, set[i].WAF, set[i].size_factor, set[i].run_num,
				(double)set[i].memory_usage_bit/(RANGE*48));
		for(uint32_t j=0; j<=set[i].num_of_level; j++){
			printf("\tentry_num:%lu run_range_size:%lf line_cnt:%lu chunk_cnt:%lu\n", 
					set[i].entry_num[j],
					set[i].run_range_size[j],
					line_cnt(set[i].run_range_size[j]),
					chunk_cnt(set[i].run_range_size[j]));
		}
	}
}

void print_tree_param_new(tree_param *set, uint32_t number, uint32_t divide){
	printf("BUFF\tLEVEL\tWAF\tsf\t#R\tbuffer\tsum\tTABLE\tENTRY\tR_AVG\n");
	for(uint32_t i=1; i<=number; i++){
		printf("%.2f\t%u\t%.2lf\t%.2lf\t%lu\t%.2lf\t%.3lf\t%.3f\t%.4f\t%.3f\n", 
				(double)divide,
				i, set[i].WAF, set[i].size_factor, set[i].run_num,
				(double)((double)RANGE*48/divide)/(RANGE*48),
				(double)((double)set[i].memory_usage_bit)/(RANGE*48),
				(double)set[i].total_run_num_bit/(RANGE*48),
				(double)set[i].entry_bit/(RANGE*48),
	//			(double)RANGE/(set[i].size_factor*i)/RANGE
				set[i].avg_run_size
				);
		/*
		for(uint32_t j=1;j<=i; j++){
			printf("\t%.3lf\t%.2lf\t%.3lf\n", set[i].run_range_size[j]*100, set[i].rh_bit[j],
					set[i].level_size_ratio[j] * 100);
		}*/
//		printf("\n");
	}
}

void lsmtree_l0_fix_sf(){
	double size_factor=2;
	printf("BUF\tL0\tLEVEL\tTABLE\tBPE\tSUM\t\n");
	for(uint32_t WB=(LOWQDEPTH*(48+48)); WB<=RANGE*48; WB*=2){
		double write_buffer_memory_bit=WB;
		double buffered_ent=write_buffer_memory_bit/(48+48);
		double chunk_num=(double)RANGE/buffered_ent;
		double max_level=get_double_level(size_factor, chunk_num);
		double max_level_double=get_double_level(size_factor, chunk_num);
		double table_bit=ceil(log2(max_level*size_factor)) * RANGE;
		double memory_size=0;
		memory_size+=write_buffer_memory_bit;
		memory_size+=table_bit;
		double plr_bit=0;


		if(max_level!=max_level_double) continue;

		uint64_t total_level_size=0;
		/*calculating */
		for(uint32_t i=1; i<=ceil(max_level); i++){
			uint64_t run_size;
			uint64_t level_size;
			if(i==ceil(max_level)){
				run_size=buffered_ent * pow(size_factor, max_level-1);
				level_size=buffered_ent * pow(size_factor, max_level);
			}
			else{
				run_size=buffered_ent * pow(size_factor, i-1);
				level_size=buffered_ent * ceil(pow(size_factor, i));
			}
			total_level_size+=level_size;
		}

		for(uint32_t i=1; i<=ceil(max_level); i++){
			uint64_t run_size;
			uint64_t level_size;

			if(i==ceil(max_level)){
				run_size=buffered_ent * pow(size_factor, max_level-1);
				level_size=buffered_ent * pow(size_factor, max_level);
			}
			else{
				run_size=buffered_ent * pow(size_factor, i-1);
				level_size=buffered_ent * ceil(pow(size_factor, i));
			}

			double run_coverage_ratio=(double)run_size/total_level_size;
			plr_bit+=level_size * MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
		}

		double bit_per_entry=plr_bit/total_level_size;
		memory_size+=bit_per_entry*RANGE;

		printf("%u\t%.3lf\t%.3lf\t%.3lf\t%.3lf\t%.3lf\n", WB/LOWQDEPTH,(float)WB/memory_size, max_level, table_bit/(RANGE*48), 
			bit_per_entry/48, memory_size/(RANGE*48));
	}
}

void lsmtree_sf_fix_l0(){
	printf("SF\tLEVEL\tTABLE\tBPE\tSUM\t\n");
	uint32_t write_buffer_memory_bit=LOWQDEPTH*(48+48);
	uint32_t buffered_ent=write_buffer_memory_bit/(48+48);
	uint32_t chunk_num=RANGE/buffered_ent+(RANGE%buffered_ent?1:0);

	for(uint32_t size_factor=2; size_factor<=1024; size_factor*=2){
		double max_level=get_double_level(size_factor, chunk_num);
		double table_bit=ceil(log2(ceil(max_level)*size_factor)) * RANGE;
		double memory_size=0;
		memory_size+=table_bit;
		memory_size+=write_buffer_memory_bit;
		double plr_bit=0;

		uint64_t total_level_size=0;
		/*calculating */
		for(uint32_t i=1; i<=ceil(max_level); i++){
			uint64_t run_size;
			uint64_t level_size;
			if(i==ceil(max_level)){
				run_size=buffered_ent * pow(size_factor, max_level-1);
				level_size=buffered_ent * pow(size_factor, max_level);
			}
			else{
				run_size=buffered_ent * pow(size_factor, i-1);
				level_size=buffered_ent * ceil(pow(size_factor, i));
			}
			total_level_size+=level_size;
		}

		for(uint32_t i=1; i<=ceil(max_level); i++){
			uint64_t run_size;
			uint64_t level_size;

			if(i==ceil(max_level)){
				run_size=buffered_ent * pow(size_factor, max_level-1);
				level_size=buffered_ent * pow(size_factor, max_level);
			}
			else{
				run_size=buffered_ent * pow(size_factor, i-1);
				level_size=buffered_ent * ceil(pow(size_factor, i));
			}
			
			double run_coverage_ratio=(double)run_size/total_level_size;
			plr_bit+=level_size * MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
		}

		double bit_per_entry=plr_bit/total_level_size;
		memory_size+=RANGE*bit_per_entry;
		printf("%u\t%.3lf\t%.3lf\t%.3lf\t%.3lf\n", size_factor, max_level, table_bit/(RANGE*48), 
				bit_per_entry/(48), memory_size/(RANGE*48));

	}
	
//	uint32_t max_level=get_level(2, chunk_num);
//	for(uint32_t level=2; level <= max_level; level++){
//		double size_factor=get_double_size_factor((double)level, (double)chunk_num);
//		double table_bit=ceil(log2(level*size_factor)) * RANGE;
//		double memory_size=0;
//		memory_size+=table_bit;
//		memory_size+=write_buffer_memory_bit;
//		double plr_bit=0;
//
//		uint64_t total_level_size=0;
//		/*calculating */
//		for(uint32_t i=1; i<=level; i++){
//			uint32_t run_size;
//			uint64_t level_size;
//
//			run_size=buffered_ent * pow(size_factor, i-1);
//			level_size=buffered_ent * ceil(pow(size_factor, i))-pow(size_factor, i-1);
//			/*
//			level_size=level_size>RANGE?RANGE:level_size;
//			run_size=run_size>RANGE?RANGE:run_size;
//			*/
//
//			total_level_size+=level_size;
//			double run_coverage_ratio=(double)run_size/RANGE;
//			plr_bit+=level_size * MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
//		}
//		double bit_per_entry=plr_bit/total_level_size;
//		memory_size+=RANGE*bit_per_entry;
//		printf("%.2lf\t%u\t%.3lf\t%.3lf\t%.3lf\n", size_factor, level, table_bit/(RANGE*48), 
//				bit_per_entry/(48), memory_size/(RANGE*48));
//	}
}


void lsmtree_tiering_only_test(){
	for(uint32_t divide=2; true ; divide*=2){
		uint32_t write_buffer_memory_bit=RANGE*48/divide;
		uint32_t buffered_ent=write_buffer_memory_bit/(48+48);
		tree_param *settings=NULL;

		if(buffered_ent < QSIZE*L2PGAP){
			break;
		}
		uint32_t chunk_num=RANGE/buffered_ent+(RANGE%buffered_ent?1:0);
		uint32_t max_level=get_level(2, chunk_num);
		max_level=max_level>20?20:max_level;
		settings=(tree_param*)calloc(max_level+1, sizeof(tree_param));

		for(uint32_t i=1; i<=max_level; i++){
			settings[i].size_factor=get_double_size_factor(i, chunk_num);
			settings[i].num_of_level=i;
			settings[i].memory_usage_bit=ceil(log2(settings[i].size_factor *i)) * RANGE;
			settings[i].total_run_num_bit=ceil(log2(settings[i].size_factor *i)) * RANGE;
			settings[i].WAF=i;
			settings[i].run_num=i*settings[i].size_factor;
			double total_level_size=0;
			settings[i].memory_usage_bit+=write_buffer_memory_bit;
			//if(settings[i].size_factor<=2) break;
	//		settings[i].lp=(level_param*)calloc(i+1, sizeof(level_param));	
			for(uint32_t j=1; j<=i; j++){
				//uint64_t num_range=RANGE;
				uint32_t run_size;
				uint64_t level_size=0;
				if(j==i){
					run_size=buffered_ent * pow(settings[i].size_factor, j-1);
					level_size=RANGE-total_level_size;
				}
				else{
					run_size=buffered_ent * pow(settings[i].size_factor, j-1);
					level_size=buffered_ent * ceil(pow(settings[i].size_factor, j)) -pow(settings[i].size_factor, j-1);
					//level_size=buffered_ent * ceil(pow(settings[i].size_factor, j));
				}

				total_level_size+=level_size;
				level_size=level_size>RANGE?RANGE:level_size;
				run_size=run_size>RANGE?RANGE:run_size;
				double run_coverage_ratio=(double)run_size/RANGE;
				uint64_t run_memory_usage_bit=level_size * MIN(bf_memory_per_ent(run_coverage_ratio),
						plr_memory_per_ent(run_coverage_ratio));
				bool plr_on=false;
				if(bf_memory_per_ent(run_coverage_ratio) > plr_memory_per_ent(run_coverage_ratio)){
					settings[i].plr_cnt++;
					plr_on=true;
				}
				settings[i].rh_bit[j]=MIN(bf_memory_per_ent(run_coverage_ratio),
						plr_memory_per_ent(run_coverage_ratio));

				settings[i].run_range_size[j]=(double)run_size/RANGE;
				settings[i].level_size_ratio[j]=(double)level_size/RANGE;
				settings[i].entry_bit+=run_memory_usage_bit;
				settings[i].memory_usage_bit+=level_size * MIN(bf_memory_per_ent(run_coverage_ratio),
						plr_memory_per_ent(run_coverage_ratio));
				if(plr_on){
					settings[i].avg_run_size+=run_size;
				}
				/*
				if(j==i){
					settings[i].last_avg_run_size=(double)level_size/settings[i].size_factor/num_range;
				}*/
			}
			if(settings[i].plr_cnt){
				settings[i].avg_run_size/=settings[i].plr_cnt;
				settings[i].avg_run_size/=RANGE;
			}
		}

		//printf("\n");
		print_tree_param_new(settings, max_level, divide);
		free(settings);
	}
}

lsmtree_parameter lsmtree_memory_limit_to_setting(uint64_t memory_limit_bit){
	lsmtree_parameter res;
	tree_param *settings=NULL;
	double min_WAF=UINT32_MAX;
	uint32_t target_level;
	tree_param target_level_param;
	target_level_param.lp=NULL;
	uint32_t target_buffered_ent;
	uint32_t target_chunk_num;
	uint32_t target_WB;
	for(uint64_t WB=(LOWQDEPTH*(48+48)); WB<=RANGE*48; WB+=(LOWQDEPTH*(48+48))){
		static int cnt=0;
		uint64_t now_memory_limit_bit=memory_limit_bit;
		uint64_t write_buffer_memory_bit=WB;
		cnt++;
	//	printf("WB:%lu\n",WB);
		if(16651776*8==WB){
			printf("tttttttarget:%u\n", cnt);
		}
		uint64_t buffered_ent=write_buffer_memory_bit/(48+48);
		if(write_buffer_memory_bit > now_memory_limit_bit) continue;

		uint64_t chunk_num=RANGE/buffered_ent+(RANGE%buffered_ent?1:0);
		uint64_t max_level=get_level(2, chunk_num);

	//	now_memory_limit_bit-=write_buffer_memory_bit;
		settings=(tree_param*)calloc(max_level+1, sizeof(tree_param));

		/*change tiering from bottom*/
		for(uint32_t i=1; i<=max_level; i++){
			settings[i].size_factor=get_size_factor(i, chunk_num);
			settings[i].num_of_level=i;
			settings[i].memory_usage_bit=0;
			settings[i].lp=(level_param*)calloc(i+1, sizeof(level_param));
			settings[i].memory_usage_bit+=write_buffer_memory_bit;
			if(settings[i].isinvalid) continue;

			settings[i].memory_usage_bit+=RANGE*ceil(log2(settings[i].size_factor * i));
			/*
			settings[i].memory_limit_for_helper=memory_limit_bit-
				(RANGE*ceil(log2(settings[i].size_factor*i)))-write_buffer_memory_bit;*/
			settings[i].run_num=settings[i].size_factor*i;
			settings[i].WAF=i+1;
			for(uint32_t j=i; j>=1; j--){
	//			uint32_t now_run_num=settings[i].run_num;
				uint64_t num_range=RANGE;
				uint64_t level_size;
				uint64_t run_size=buffered_ent * pow(settings[i].size_factor, j-1);

				if(j==i){
					level_size=buffered_ent * ceil(pow(settings[i].size_factor, j));
				}
				else{
					level_size=buffered_ent * ceil(pow(settings[i].size_factor, j))-
						pow(settings[i].size_factor, j-1);
				}

				level_size=level_size>RANGE?RANGE:level_size;
				run_size=run_size>RANGE?RANGE:run_size;

				double run_coverage_ratio=(double)run_size/num_range;


				settings[i].lp[j].level_type=TIERING;
				settings[i].lp[j].is_wisckey=false;
				if(bf_memory_per_ent(run_coverage_ratio) < plr_memory_per_ent(run_coverage_ratio)){
					settings[i].lp[j].is_bf=true;
					settings[i].BF_memory+=bf_memory_per_ent(run_coverage_ratio)*level_size;
				}
				else{
					settings[i].lp[j].is_bf=false;
					settings[i].PLR_memory+=plr_memory_per_ent(run_coverage_ratio)*level_size;
				}
				uint64_t run_memory_usage_bit= level_size *
					MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
				settings[i].memory_usage_bit+=run_memory_usage_bit;
				settings[i].entry_num[j]=level_size;
				settings[i].run_range_size[j]=run_coverage_ratio;
			}

			if(settings[i].memory_usage_bit > now_memory_limit_bit){
				settings[i].isinvalid=true;
			}
		}


		for(uint32_t i=1; i<=max_level; i++){
			if(settings[i].isinvalid) continue;
			if(min_WAF > settings[i].WAF ||
					(min_WAF==settings[i].WAF && target_WB > WB)){
				min_WAF=settings[i].WAF;
				target_level=i;
				if(target_level_param.lp){
					free(target_level_param.lp);
				}
				target_level_param=settings[i];
				target_level_param.lp=(level_param*)malloc(sizeof(level_param) * (i+1));
				memcpy(target_level_param.lp, settings[i].lp, sizeof(level_param) * (i+1));
				target_buffered_ent=buffered_ent;
				target_chunk_num=chunk_num;
				target_WB=WB;
			}
		}

		for(uint32_t i=1; i<=max_level; i++){
			free(settings[i].lp);
		}
		free(settings);
	}

	printf("target settings memory usage!\n");
	print_tree_param(&target_level_param, 1);

	/*setting up lsmtree_parameter*/
	target_level_param.size_factor=round(target_level_param.size_factor);
	res.tr=target_level_param;
	//res.tr.memory_limit_for_helper-=res.tr.BF_memory;

	res.bf_ptr_guard_rhp.type=HELPER_BF_PTR_GUARD;
	res.bf_ptr_guard_rhp.target_prob=TARGETFPR;
	res.bf_ptr_guard_rhp.member_num=KP_IN_PAGE;

	res.bf_guard_rhp.type=HELPER_BF_ONLY_GUARD;
	res.bf_guard_rhp.target_prob=TARGETFPR;
	res.bf_guard_rhp.member_num=KP_IN_PAGE;

	res.plr_rhp.type=HELPER_PLR;
	res.plr_rhp.slop_bit=8;
	res.plr_rhp.range=(uint32_t)((double)TARGETFPR*100/2);
	res.plr_rhp.member_num=KP_IN_PAGE;


	res.LEVELN=target_level;
	res.mapping_num=target_chunk_num;
	res.last_size_factor=res.normal_size_factor=res.tr.size_factor;
	res.version_enable=true;
	res.write_buffer_bit=target_WB;
	res.write_buffer_ent=target_buffered_ent-(target_buffered_ent/KP_IN_PAGE*L2PGAP);
#ifdef MIN_ENTRY_PER_SST
	res.max_sst_in_pinned_level=CEILING_TARGET(res.write_buffer_ent, MIN_ENTRY_PER_SST);
#endif
	res.read_amplification=TARGETFPR;

	res.reclaim_ppa_target=(target_buffered_ent*ceil(pow(res.tr.size_factor, target_level-1)));
	res.reclaim_ppa_target=(res.reclaim_ppa_target/QDEPTH+(res.reclaim_ppa_target%QDEPTH?1:0))*QDEPTH;
	res.reclaim_ppa_target=(res.reclaim_ppa_target/L2PGAP)+(res.reclaim_ppa_target/KP_IN_PAGE);
#ifdef DYNAMIC_HELPER_ASSIGN
	for(uint32_t i=1; i<=1000; i++){
		uint64_t bf_memory=bf_memory_per_ent((float)i/1000);
		uint64_t plr_memory=plr_memory_per_ent((float)i/1000);
		if(bf_memory > plr_memory){
			res.BF_PLR_border=(float)i/1000;
			break;
		}
	}
#endif

	//free(settings);
	
	//lsmtree_sf_fix_l0();
	//printf("\n");
	//lsmtree_l0_fix_sf();
	//printf("\n");
	lsmtree_tiering_memory_waf_calculator(4);
	return res;
}

#if 0 //below code for leveling+tiering code
lsmtree_parameter lsmtree_memory_limit_to_setting(uint64_t memory_limit_bit){
	lsmtree_parameter res;
	tree_param *settings=NULL;
	double min_WAF=UINT32_MAX;
	uint32_t target_level;
	tree_param target_level_param;
	target_level_param.lp=NULL;
	uint32_t target_buffered_ent;
	uint32_t target_chunk_num;
	uint32_t target_divide;
	for(uint32_t divide=2; true; divide*=2){
		uint32_t now_memory_limit_bit=memory_limit_bit;
		uint32_t write_buffer_memory_bit=RANGE*48/divide;
		uint32_t buffered_ent=write_buffer_memory_bit/(48+48);
		if(write_buffer_memory_bit > now_memory_limit_bit) continue;

		if(buffered_ent < QSIZE*L2PGAP){
			break;
		}
		uint32_t chunk_num=RANGE/buffered_ent+(RANGE%buffered_ent?1:0);
		uint32_t max_level=get_level(2, chunk_num);

	//	now_memory_limit_bit-=write_buffer_memory_bit;
		settings=(tree_param*)calloc(max_level+1, sizeof(tree_param));
		/*start all leveling*/
		for(uint32_t i=1; i<=max_level; i++){
			settings[i].size_factor=get_size_factor(i, chunk_num);
			settings[i].num_of_level=i;
			settings[i].memory_usage_bit=0;
			settings[i].lp=(level_param*)calloc(i+1, sizeof(level_param));
			settings[i].memory_usage_bit+=write_buffer_memory_bit;
			for(uint32_t j=i; j>=1; j--){
				uint64_t num_range=RANGE;
				uint64_t covered_range=buffered_ent * ceil(pow(settings[i].size_factor, j)-pow(settings[i].size_factor, j-1));
				covered_range=covered_range>RANGE?RANGE:covered_range;
				double coverage_ratio=(double)covered_range/num_range;
				settings[i].lp[j].level_type=LEVELING;
				settings[i].lp[j].is_wisckey=false;
				settings[i].lp[j].is_bf=false;
				if(bf_memory_per_ent(coverage_ratio) < plr_memory_per_ent(coverage_ratio)){
					settings[i].lp[j].is_bf=true;
				}
				settings[i].memory_usage_bit+= covered_range * 
					MIN(bf_memory_per_ent(coverage_ratio), plr_memory_per_ent(coverage_ratio));
			}
			settings[i].memory_usage_bit+=RANGE*ceil(log2(i));
			settings[i].WAF=i*settings[i].size_factor;
			settings[i].run_num=i;

			if(settings[i].memory_usage_bit > now_memory_limit_bit){
				settings[i].isinvalid=true;
			}
		}
		/*
		   printf("after_leveling\n");
		   print_tree_param(settings, max_level+1);*/

		/*change tiering from bottom*/
		for(uint32_t i=1; i<=max_level; i++){
			if(settings[i].isinvalid) continue;
			for(uint32_t j=i; j>=1; j--){
				uint32_t now_run_num=settings[i].run_num;
				uint64_t num_range=RANGE;
				uint64_t level_size;
				uint64_t run_size=buffered_ent * pow(settings[i].size_factor, j-1);

				if(j==i){
					level_size=buffered_ent * ceil(pow(settings[i].size_factor, j));
				}
				else{
					level_size=buffered_ent * ceil(pow(settings[i].size_factor, j))-
						pow(settings[i].size_factor, j-1);
				}

				level_size=level_size>RANGE?RANGE:level_size;
				run_size=run_size>RANGE?RANGE:run_size;

				double level_coverage_ratio=(double)level_size/num_range;
				double run_coverage_ratio=(double)run_size/num_range;

				uint64_t level_memory_usage_bit= level_size *
					MIN(bf_memory_per_ent(level_coverage_ratio), plr_memory_per_ent(level_coverage_ratio));
				uint64_t run_memory_usage_bit= level_size *
					MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));

				uint64_t prev_table_memory_bit=RANGE*ceil(log2(now_run_num));
				uint64_t table_memory_bit=RANGE * ceil(log2(now_run_num-1+floor(settings[i].size_factor)));

				if(settings[i].memory_usage_bit - prev_table_memory_bit - level_memory_usage_bit 
						+ table_memory_bit + run_memory_usage_bit < now_memory_limit_bit){
					settings[i].lp[j].level_type=TIERING;
					if(bf_memory_per_ent(run_coverage_ratio) < plr_memory_per_ent(run_coverage_ratio)){
						settings[i].lp[j].is_bf=true;
					}
					settings[i].WAF-=(settings[i].size_factor-1); //changing leveling to tiering;
					if(j==i){
						settings[i].WAF+=1;
					}
					settings[i].memory_usage_bit=settings[i].memory_usage_bit-prev_table_memory_bit-level_memory_usage_bit 
						+table_memory_bit+run_memory_usage_bit; //update memory usage
					settings[i].run_num=settings[i].run_num-1+floor(settings[i].size_factor); //update run_num
				}
			}
		}


		printf("write_buffer_divide :%u\n", divide);
		print_tree_param(settings, max_level+1);

		for(uint32_t i=1; i<=max_level; i++){
			if(settings[i].isinvalid) continue;
			if(min_WAF > settings[i].WAF ||
					(min_WAF==settings[i].WAF && target_divide < divide)){
				min_WAF=settings[i].WAF;
				target_level=i;
				if(target_level_param.lp){
					free(target_level_param.lp);
				}
				target_level_param=settings[i];
				target_level_param.lp=(level_param*)malloc(sizeof(level_param) * (i+1));
				memcpy(target_level_param.lp, settings[i].lp, sizeof(level_param) * (i+1));
				target_buffered_ent=buffered_ent;
				target_chunk_num=chunk_num;
				target_divide=divide;
			}
		}

		for(uint32_t i=1; i<=max_level; i++){
			free(settings[i].lp);
		}
		free(settings);
	}

	printf("target settings memory usage!\n");
	print_tree_param(&target_level_param, 1);

	/*setting up lsmtree_parameter*/
	res.tr=target_level_param;

	res.bf_ptr_guard_rhp.type=HELPER_BF_PTR_GUARD;
	res.bf_ptr_guard_rhp.target_prob=TARGETFPR;
	res.bf_ptr_guard_rhp.member_num=KP_IN_PAGE;

	res.bf_guard_rhp.type=HELPER_BF_ONLY_GUARD;
	res.bf_guard_rhp.target_prob=TARGETFPR;
	res.bf_guard_rhp.member_num=KP_IN_PAGE;

	//res.plr_rhp=res.bf_ptr_guard_rhp;
	
	res.plr_rhp.type=HELPER_PLR;
	res.plr_rhp.slop_bit=8;
	res.plr_rhp.range=(uint32_t)((double)TARGETFPR*100/2);
	res.plr_rhp.member_num=KP_IN_PAGE;


	res.LEVELN=target_level;
	res.mapping_num=target_chunk_num;
	res.last_size_factor=res.normal_size_factor=res.tr.size_factor;
	res.version_enable=true;
	res.write_buffer_divide=target_divide;
	res.write_buffer_ent=target_buffered_ent-(target_buffered_ent/KP_IN_PAGE*L2PGAP);
	res.read_amplification=TARGETFPR;

	res.reclaim_ppa_target=target_buffered_ent*ceil(pow(res.tr.size_factor, target_level-1));

	free(settings);
	//lsmtree_tiering_only_test();
	//lsmtree_sf_fix_l0();
	//lsmtree_l0_fix_sf();
	return res;
}
#endif
