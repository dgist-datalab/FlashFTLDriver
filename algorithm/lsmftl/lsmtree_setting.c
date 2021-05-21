#include "lsmtree.h"
#include "io.h"
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include "function_test.h"
#include "segment_level_manager.h"
#include "./design_knob/design_knob.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
extern lsmtree LSM;
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
	printf("[param] border of leveling: %u\n",tr.border_of_leveling);
	printf("[param] border of wisckey: %u\n",tr.border_of_wisckey);
	printf("[param] border of bf: %u\n",tr.border_of_bf);

	printf("[param] normal size factor: %u\n", LSM.param.normal_size_factor);
	printf("[param] read amplification: %f\n", LSM.param.read_amplification+1);
	printf("[param] version number :%lu\n", tr.run_num);
	printf("[param] write_buffer ent: %u\n", LSM.param.write_buffer_ent);

	printf("[PERF] WAF:%u+GC\n", tr.WAF);
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

uint32_t lsmtree_argument_set(int argc, char *argv[]){
	int c;
	uint64_t target_memory_usage_bit=0;
	uint32_t percentage=30;
	while((c=getopt(argc,argv,"mMhH"))!=-1){
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
		}
	}
	
	target_memory_usage_bit=(uint64_t)((double)RANGE*48/100*percentage);
	LSM.param=lsmtree_memory_limit_to_setting(target_memory_usage_bit);

	printf("------------------------------------------\n");
	print_level_param();
	printf("------------------------------------------\n");
	return 1;
}

lsmtree_parameter lsmtree_memory_limit_to_setting(uint64_t memory_limit_bit){
	lsmtree_parameter res;
	uint32_t write_buffer_memory_bit=RANGE*48/256;
	uint32_t buffered_ent=write_buffer_memory_bit/(48+48);
	uint32_t chunk_num=RANGE/buffered_ent+(RANGE%buffered_ent?1:0);
	uint32_t max_level=get_level(2, chunk_num);

	
	tree_param *settings=(tree_param*)calloc(max_level, sizeof(tree_param));
	/*start all leveling*/
	for(uint32_t i=1; i<=max_level; i++){
		settings[i].size_factor=get_size_factor(i, chunk_num);
		settings[i].num_of_level=i;
		settings[i].border_of_leveling=i;
		settings[i].memory_usage_bit=0;
		settings[i].border_of_wisckey=0;
		for(uint32_t j=i; j>=1; j--){
			uint64_t num_range=RANGE;
			uint64_t covered_range=buffered_ent * pow(settings[i].size_factor, j);
			double coverage_ratio=(double)covered_range/num_range;
			if(bf_memory_per_ent(coverage_ratio) < plr_memory_per_ent(coverage_ratio)){
				settings[i].border_of_bf=j;
			}
			settings[i].memory_usage_bit+= covered_range * 
				MIN(bf_memory_per_ent(coverage_ratio), plr_memory_per_ent(coverage_ratio));
		}
		settings[i].memory_usage_bit+=RANGE*ceil(log2(i));
		settings[i].WAF=i*settings[i].size_factor;
		settings[i].run_num=i;

		if(settings[i].memory_usage_bit > memory_limit_bit){
			settings[i].isinvalid=true;
		}

	}

	/*change tiering from bottom*/
	for(uint32_t i=1; i<=max_level; i++){
		if(settings[i].isinvalid) continue;
		for(uint32_t j=i; j>=1; j--){
			uint32_t now_run_num=settings[i].run_num;
			uint64_t num_range=RANGE;
			uint64_t level_size=buffered_ent * pow(settings[i].size_factor, j);
			uint64_t run_size=buffered_ent * pow(settings[i].size_factor, j-1);
			
			double level_coverage_ratio=(double)level_size/num_range;
			double run_coverage_ratio=(double)run_size/num_range;
			
			uint64_t level_memory_usage_bit= level_size *
				MIN(bf_memory_per_ent(level_coverage_ratio), plr_memory_per_ent(level_coverage_ratio));
			uint64_t run_memory_usage_bit= level_size *
				MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
			
			uint64_t prev_table_memory_bit=RANGE*ceil(log2(now_run_num));
			uint64_t table_memory_bit=RANGE * ceil(log2(now_run_num-1+settings[i].size_factor));
			
			if(settings[i].memory_usage_bit - prev_table_memory_bit - level_memory_usage_bit 
					+ table_memory_bit + run_memory_usage_bit < memory_limit_bit){
				settings[i].border_of_leveling=j-1; //update leveling border
				if(bf_memory_per_ent(run_coverage_ratio) < plr_memory_per_ent(run_coverage_ratio)){
					settings[i].border_of_bf=j; //update border of bf
				}
				settings[i].WAF-=(settings[i].size_factor-1); //changing leveling to tiering;
				if(j==i){
					settings[i].WAF+=1;
				}
				settings[i].memory_usage_bit=settings[i].memory_usage_bit-prev_table_memory_bit-level_memory_usage_bit 
					+table_memory_bit+run_memory_usage_bit; //update memory usage
				settings[i].run_num=settings[i].run_num-1+settings[i].size_factor; //update run_num
			}
		}
	}

	/*changing wisckey from top*/
	for(uint32_t i=1; i<=max_level; i++){
		if(settings[i].isinvalid) continue;
		for(uint32_t j=1; j<=i; j++){
			uint64_t level_size=buffered_ent * pow(settings[i].size_factor, j);
			if(settings[i].memory_usage_bit + level_size * 48 < memory_limit_bit){
				settings[i].memory_usage_bit+=level_size * 48;
				settings[i].WAF-=(j<=settings[i].border_of_leveling ? settings[i].size_factor: 1);
				settings[i].border_of_wisckey=j;
			}
		}
	}

	uint32_t min_WAF=UINT32_MAX;
	uint32_t target_level=0;
	/*find min WAF and setting params*/
	for(uint32_t i=1; i<=max_level; i++){
		if(min_WAF > settings[i].WAF){
			min_WAF=settings[i].WAF;
			target_level=i;
		}
	}

	/*setting up lsmtree_parameter*/
	res.tr=settings[target_level];

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
	res.mapping_num=chunk_num;
	res.last_size_factor=res.normal_size_factor=res.tr.size_factor;
	res.version_enable=true;
	res.write_buffer_ent=buffered_ent;

	free(settings);
	return res;
}
#if 0
uint32_t __lsmtree_argument_set(int argc, char *argv[]){
	/*
	lsmtree_param_default_setting();
	printf("------------------------------------------\n");
	print_level_param();
	printf("------------------------------------------\n");
	return 1;*/
	int c;
	bool leveln_setting=false, sizef_setting=false, reada_setting=false, rh_setting=false;
	bool version_setting=false;
	while((c=getopt(argc,argv,"LSRahbr"))!=-1){
		switch(c){
			case 'h':
				print_help();
				exit(1);
				break;
			case 'L':
				LSM.param.LEVELN=atoi(argv[optind]);
				leveln_setting=true;
				break;
			case 'S':
				LSM.param.normal_size_factor=atoi(argv[optind]);
				sizef_setting=true;
				break;
			case 'b':
				LSM.param.plr_bit=atoi(argv[optind]);
				break;
			case 'r':
				LSM.param.error_range=atoi(argv[optind]);
				break;
			case 'R':
				LSM.param.read_helper_type=atoi(argv[optind]);
				rh_setting=true;
				break;
			case 'a':
				LSM.param.read_amplification=atof(argv[optind]);
				reada_setting=true;
				break;
		}
	}


	printf("------------------------------------------\n");
	/*rhp setting*/
	if((leveln_setting && sizef_setting)){
		EPRINT("it cannot setting levelnum and sizefactor\n", true);
	}
	else if(!leveln_setting && !sizef_setting){
		printf("no sizefactor & no # of level\n");
		printf("# of level is set as 3\n");
		LSM.param.LEVELN=3;
		LSM.param.write_buffer_ent=KP_IN_PAGE;
		LSM.param.mapping_num=(TOTALSIZE/LPAGESIZE/LSM.param.write_buffer_ent);

		LSM.param.last_size_factor=
			LSM.param.normal_size_factor=
			get_size_factor(LSM.param.LEVELN, LSM.param.mapping_num);
//		LSM.param.version_number=LSM.param.LEVELN*LSM.param.normal_size_factor;
	}
	else if(leveln_setting){
	//	LSM.param.last_size_factor=32-1-LSM.param.LEVELN;
		LSM.param.write_buffer_ent=KP_IN_PAGE;
		LSM.param.mapping_num=(TOTALSIZE/LPAGESIZE/LSM.param.write_buffer_ent);

		LSM.param.last_size_factor=
			LSM.param.normal_size_factor=
			get_size_factor(LSM.param.LEVELN, LSM.param.mapping_num);
	//	LSM.param.version_number=LSM.param.LEVELN*LSM.param.normal_size_factor;
	}
	else if(sizef_setting){
		EPRINT("size factor cannot be set", true);
		//LSM.param.LEVELN=get_level(LSM.param.normal_size_factor, LSM.param.mapping_num);
		//LSM.param.LEVELN++;
	}

	if(!reada_setting){
		printf("no read amplification setting - set read amp as %f\n", TARGETFPR);
		LSM.param.read_amplification=TARGETFPR;
	}

	LSM.param.version_enable=true;
	LSM.param.leveling_rhp.type=HELPER_BF_PTR_GUARD;
	LSM.param.leveling_rhp.target_prob=LSM.param.read_amplification;
	LSM.param.leveling_rhp.member_num=KP_IN_PAGE;

	if(!rh_setting){
#if 0
		LSM.param.tiering_rhp.type=HELPER_BF_ONLY_GUARD;
		if(LSM.param.version_enable){
			LSM.param.tiering_rhp.target_prob=LSM.param.read_amplification;
		}
		else{
			LSM.param.tiering_rhp.target_prob=LSM.param.read_amplification/(LSM.param.LEVELN-1+LSM.param.last_size_factor);
		}
		LSM.param.tiering_rhp.member_num=KP_IN_PAGE;
#else
		LSM.param.tiering_rhp.type=HELPER_PLR;
		LSM.param.tiering_rhp.slop_bit=8;
		LSM.param.tiering_rhp.range=5;
		LSM.param.tiering_rhp.member_num=KP_IN_PAGE;
#endif
 
	}
	else{
		LSM.param.tiering_rhp.type=LSM.param.read_helper_type;
		LSM.param.tiering_rhp.member_num=KP_IN_PAGE;
	}

	if(LSM.param.tiering_rhp.type==HELPER_PLR){
		LSM.param.tiering_rhp.slop_bit=LSM.param.plr_bit?LSM.param.plr_bit:7;
		LSM.param.tiering_rhp.range=5; //LSM.param.error_range?LSM.param.error_range:50;
	}

	printf("------------------------------------------\n");
	print_level_param();
	printf("------------------------------------------\n");

	return 1; 
}

#endif
