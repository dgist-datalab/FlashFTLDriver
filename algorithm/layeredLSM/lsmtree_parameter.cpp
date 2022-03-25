#include <math.h>
#include "./lsmtree.h"
#include "./design_knob/design_knob.h"
#include "./design_knob/lsmtree_param_module.h"
#include "./design_knob/lsmtree_param_module_double.h"
void lsmtree_print_param(lsmtree_parameter param){
	printf("=========================\n");
	printf("# of levels:%u\n",param.total_level_num);
	printf("# of runs:%u - spare run:%u\n", param.total_run_num, param.spare_run_num);
	printf("target fpr:%.2f\n", param.fpr);
	printf("size factor:%.2f\n", param.size_factor);
	printf("mem per ent:%.2f, %u\n", (double)param.max_memory_usage_bit/RANGE, param.target_bit);
	printf("memtable entry:%u\n", param.memtable_entry_num);
	printf("memory usage:%.2f\n", (double)param.max_memory_usage_bit/(RANGE*param.target_bit));
	printf("\t usage:absolute, vs page, breakdown\n");
	uint64_t temp=param.L0_bit;
	printf("\t L0:%lu %.2f %.2f\n", temp, (double)temp/(RANGE*param.target_bit), 
			(double)temp/param.max_memory_usage_bit);
	temp=param.shortcut_bit;
	printf("\t sc:%lu %.2f %.2f\n", temp, (double)temp/(RANGE*param.target_bit), 
			(double)temp/param.max_memory_usage_bit);
	temp=param.BF_bit;
	printf("\t BF:%lu %.2f %.2f\n", temp, (double)temp/(RANGE*param.target_bit), 
			(double)temp/param.max_memory_usage_bit);
	temp=param.PLR_bit;
	printf("\t PLR:%lu %.2f %.2f\n", temp, (double)temp/(RANGE*param.target_bit), 
			(double)temp/param.max_memory_usage_bit);
	printf("BF level: %u~%u\n", param.BF_level_range.start, param.BF_level_range.end);
	printf("PLR level: %u~%u\n", param.PLR_level_range.start, param.PLR_level_range.end);
	printf("=========================\n");
}

static inline uint64_t calculating_level_memory(uint32_t level_idx, uint32_t target_bit,
		uint32_t memtable_entry_num, uint32_t size_factor, uint64_t total_lba_range, bool *isbf){
	uint64_t memory_usage_bit=0;
	uint64_t level_entry_num=(uint64_t)memtable_entry_num * pow(size_factor, level_idx);
	uint64_t run_entry_num=(uint64_t)memtable_entry_num*pow(size_factor,level_idx-1);
	double coverage=(double)run_entry_num/total_lba_range;
	double plr_memory=plr_memory_per_ent(coverage, target_bit);
	double bf_memory=bf_memory_per_ent(coverage, target_bit);

	if(plr_memory < bf_memory){
		*isbf=false;
		memory_usage_bit=plr_memory*level_entry_num;
	}
	else{
		*isbf=true;
		memory_usage_bit=bf_memory*level_entry_num;
	}
	return memory_usage_bit;
}

lsmtree_parameter lsmtree_calculate_parameter2(float fpr, uint32_t target_bit, 
		uint64_t memory_usage_bit, uint64_t max_LBA_num){

	lsmtree_parameter res;
	memset(&res, 0, sizeof(res));
	init_memory_info(fpr * 100, target_bit);
	res.total_level_num=100;
	uint32_t entry_in_block=_PPB*L2PGAP;
	for(uint32_t i=entry_in_block; i<=RANGE/3; i+=entry_in_block){
		lsmtree_parameter temp_res;
		memset(&temp_res, 0, sizeof(temp_res));
		temp_res.fpr=fpr;
		temp_res.target_bit=target_bit;
		temp_res.memtable_entry_num=i;
		temp_res.max_memory_usage_bit+=i*(temp_res.target_bit*2);
		for(uint32_t size_factor=2; size_factor<RANGE/i; size_factor++){
			temp_res.size_factor=size_factor;
			double chunk_num=RANGE/temp_res.memtable_entry_num;
	//		printf("chunk_num :%lf\n", chunk_num);
			temp_res.total_level_num=ceil(get_double_level(size_factor, chunk_num));
			temp_res.total_run_num=temp_res.total_level_num*size_factor;
			uint32_t table_bit=ceil(log2(temp_res.total_run_num));
			temp_res.shortcut_bit=table_bit * RANGE;
			temp_res.max_memory_usage_bit+=temp_res.shortcut_bit;
			temp_res.spare_run_num=(int32_t)(1<<table_bit)-temp_res.total_run_num;


			temp_res.BF_level_range.start=1;
			/*caculate total entry_num*/
			uint64_t fixed_total_entry_num=0;
			for(uint32_t j=1; j<=temp_res.total_level_num; j++){
				fixed_total_entry_num+=temp_res.memtable_entry_num * pow(temp_res.size_factor, j);
			}

			uint64_t total_level_memory_bit=0;
			for(uint32_t j=1; j<=temp_res.total_level_num; j++){
				bool isbf=true;
				uint64_t level_memory_bit=calculating_level_memory(j, temp_res.target_bit,
						temp_res.memtable_entry_num, temp_res.size_factor, fixed_total_entry_num, &isbf);
				//temp_res.max_memory_usage_bit+=level_memory_bit;
				total_level_memory_bit+=level_memory_bit;
				if(isbf){
					temp_res.BF_bit+=level_memory_bit;
					temp_res.BF_level_range.end=j;
				}
				else{
					temp_res.PLR_bit+=level_memory_bit;
					temp_res.PLR_level_range.end=j;
				}
			}
			temp_res.PLR_level_range.start=temp_res.BF_level_range.end+1;

			/*adjust memory*/
			double bf_ratio=(double)temp_res.BF_bit/total_level_memory_bit;
			double plr_ratio=(double)temp_res.PLR_bit/total_level_memory_bit;
			double bit_per_ent=(double)total_level_memory_bit/fixed_total_entry_num;

			double adjust_total_level_memory=bit_per_ent*RANGE;
			temp_res.max_memory_usage_bit+=adjust_total_level_memory;
			temp_res.BF_bit=adjust_total_level_memory*bf_ratio;
			temp_res.PLR_bit=adjust_total_level_memory*plr_ratio;
/*
			if((double)temp_res.max_memory_usage_bit/RANGE < (double)target_bit*((double)memory_usage_bit/(RANGE*target_bit))){
				printf("%.2lf(%u, %u) -> %.2lf\n", (double)temp_res.max_memory_usage_bit / RANGE, temp_res.total_level_num,
					   temp_res.size_factor, (double)target_bit*((double)memory_usage_bit/(RANGE*target_bit)));
			}
*/
			if(temp_res.max_memory_usage_bit < memory_usage_bit){
				if(res.total_level_num > temp_res.total_level_num){
					res=temp_res;
				}
			}
		}
	}
	if(res.fpr==0){
		EPRINT("not found", false);
	}
	return res;
}


lsmtree_parameter lsmtree_calculate_parameter(float fpr, uint32_t target_bit, uint64_t memory_usage_bit, uint64_t max_LBA_num){
	lsmtree_parameter res;
	res.total_level_num=100;
	double target_avg_bit=(double)target_bit * ((double)memory_usage_bit/(RANGE*target_bit));
	uint32_t shortcut_max=floor(target_avg_bit);
	init_memory_info(fpr * 100, target_bit);
	double bf_advance=bf_advance_ratio(target_bit);
	for(uint32_t j=(1<<shortcut_max); j>1; j--){
		uint32_t bit_res=bit_calculate(j);
		uint32_t sc_num=j-2;
		double remain_avg_bit=target_avg_bit-bit_res;

		for(uint32_t max_level_num=1; max_level_num<=sc_num/2; max_level_num++){
			lsmtree_parameter temp;
			temp.fpr=fpr;
			temp.target_bit=target_bit;

			double run_per_level = (double)(sc_num) / max_level_num;
			temp.size_factor=run_per_level;
	
			temp.total_level_num=max_level_num;
			temp.total_run_num=floor(run_per_level)*max_level_num;
			temp.spare_run_num=(1<<bit_res)-temp.total_run_num;
			temp.shortcut_bit=bit_res*RANGE;

			double level_avg_bit = 0;
			double total_range = 1;
			double l0_bit=0;
			double plr_bit=0;
			double bf_bit=0;
			uint32_t bf_level=0;
			if(max_level_num==2){
				//GDB_MAKE_BREAKPOINT;
			}
			for (int32_t level = max_level_num; level >= 0; level--)
			{
				double cover_run_ratio = (double)(total_range - total_range / (run_per_level)) / run_per_level;
				double cover_level_ratio = cover_run_ratio * run_per_level;
				if (level == 0)
				{
					/*
					temp.memtable_entry_num=(uint32_t)(total_range*RANGE)/(_PPB*L2PGAP) *(_PPB*L2PGAP);
					level_avg_bit += total_range * (target_bit+ceil(log2(temp.memtable_entry_num))+PTR_BIT);
					l0_bit=total_range*(target_bit+ceil(log2(temp.memtable_entry_num))+PTR_BIT);
					*/
					
					level_avg_bit += total_range * (2*target_bit+PTR_BIT);
					l0_bit=total_range*(2*target_bit+PTR_BIT);
					temp.memtable_entry_num=(uint32_t)(total_range*RANGE)/(_PPB*L2PGAP) *(_PPB*L2PGAP);
					
				}
				else if (cover_run_ratio > bf_advance)
				{
					double plr_memory = plr_memory_per_ent(cover_run_ratio, target_bit);
					plr_bit+=plr_memory * cover_level_ratio;
					level_avg_bit += plr_memory * cover_level_ratio;
				}
				else{
					double bf_memory = bf_memory_per_ent(cover_run_ratio, target_bit);
					bf_bit+=bf_memory * cover_level_ratio;
					level_avg_bit += bf_memory * cover_level_ratio;
					bf_level++;
				}
				total_range /= run_per_level;
			}

			temp.PLR_level_range.start=bf_level+1;
			temp.PLR_level_range.end=max_level_num;
			temp.BF_level_range.start=bf_level==0?0:1;
			temp.BF_level_range.end=bf_level;

			temp.BF_bit=bf_bit*RANGE;
			temp.per_bf_bit=bf_bit;
			temp.PLR_bit=plr_bit*RANGE;
			temp.per_plr_bit=plr_bit;
			temp.L0_bit=l0_bit*RANGE;
			temp.max_memory_usage_bit=(level_avg_bit+bit_res)*RANGE;
			//printf("level bit:%.2f remain_bit:%.2f \t", level_avg_bit, remain_avg_bit);
			//printf("L0:%.2f BF:%.2f PLR:%.2f\n", l0_bit, bf_bit, plr_bit);
			if(level_avg_bit <= remain_avg_bit && temp.memtable_entry_num){
				if(res.total_level_num > temp.total_level_num){
					res=temp;
				}
				else if(res.total_level_num==temp.total_level_num){
					if(res.max_memory_usage_bit > temp.max_memory_usage_bit){
						res=temp;
					}
					else if(res.BF_level_range.start==0 && temp.BF_level_range.start!=0){
						res=temp;
					}
					/*
					if(res.memtable_entry_num > temp.memtable_entry_num){
						res=temp;
					}*/
					/*
					if(res.size_factor < temp.size_factor){
						res=temp;
					}*/
				}
				break;
			}
		}
	}
	return res;
}


static void print_help(){
	printf("-----help-----\n");
	printf("parameters (f, m, b, t)\n");
	printf("-f: set read amplification (float type)\n");
	printf("-m: memory usage percentage compare PageFTL\n");
	printf("-b: target bit num for lba\n");
	printf("-t: non zero value for parameter test\n");
}

extern lsmtree_parameter *target_param;
uint32_t gc_type;
uint32_t lsmtree_argument_set(int argc, char **argv){
	int c;
	float fpr=0.1;
	uint32_t test_flag=0;
	uint32_t target_bit=33;//bit_calculate(RANGE);
	uint32_t memory_usage=29;
	while((c=getopt(argc,argv,"hHmMtTfFbBGg"))!=-1){
		switch(c){
			case 'h':
			case 'H':
				print_help();
				exit(1);
				break;
			case 't':
			case 'T':
				test_flag=atoi(argv[optind]);
				break;
			case 'm':
			case 'M':
				memory_usage=atoi(argv[optind]);
				break;
			case 'g':
			case 'G':
				gc_type=atoi(argv[optind]);
				break;
			case 'f':
			case 'F':
				fpr=atof(argv[optind]);
				break;
			case 'b':
				target_bit=atoi(argv[optind]);
				break;
			default:
				printf("invalid parameters\n");
				print_help();
				exit(1);
				break;
		}
	}
	printf("gc_type:%u\n",gc_type);
	//lsmtree_calculate_parameter2(fpr, target_bit, RANGE*target_bit/100*memory_usage,RANGE);

	lsmtree_parameter param=lsmtree_calculate_parameter(fpr, target_bit, RANGE*target_bit/100*memory_usage,RANGE);
	lsmtree_print_param(param);

	if(!test_flag){
		target_param=(lsmtree_parameter*)calloc(1, sizeof(lsmtree_parameter));
		memcpy(target_param, &param, sizeof(lsmtree_parameter));
	}
	else{
		exit(1);
	}

	return 1;
}
