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
extern float TARGETFPR;
typedef struct parameter_combination_info{
	uint32_t max_level;
	uint32_t size_factor;
	double total_memory;
	double bf_memory;
	double plr_memory;
	double wb_memory;
	double table_memory;
	double total_level_size;
}_pci;


void lsmtree_tiering_memory_waf_calculator(uint32_t limit_round){
	double sf_multiple=12;
	uint32_t wb_multiple=32;
	//printf("BUF\tSF\tLEVEL\tTABLE\tBPE\tSUM\t\n");
	_pci **pci_set=(_pci**)calloc(limit_round, sizeof(_pci*));
	/*pci_set[L0-idx][SF-idx]*/
	for(uint32_t i=0; i<limit_round; i++){
		pci_set[i]=(_pci*)calloc(limit_round, sizeof(_pci));	
	}

	printf("memory usage---\n");
	printf("\t");
	for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor+=sf_multiple, i++){
		printf("%u\t", size_factor);
	}
	printf("\n");
	for(uint64_t WB=130092/*(LOWQDEPTH*(48+48))*/, j=0; WB<=RANGE*48 && j<limit_round; WB*=wb_multiple, j++){	
		double write_buffer_memory_bit=WB;
		double buffered_ent=write_buffer_memory_bit/(48+48);
		double chunk_num=(double)RANGE/buffered_ent;
		printf("%.0f\t",(float)WB/(48+48)*4/K);
		for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor+=sf_multiple, i++){
			double sum_plr_memory=0;
			double sum_bf_memory=0;

			uint64_t num_bf_entry=0;
			uint64_t num_plr_entry=0;

			double max_level=get_double_level(size_factor, chunk_num);
			double table_bit=ceil(log2(ceil(max_level)*size_factor)) * RANGE;
			double memory_size=0;
			memory_size+=table_bit;
			memory_size+=write_buffer_memory_bit;
			double plr_bit=0;

			double total_level_size=0;
			pci_set[j][i].size_factor=size_factor;
			pci_set[j][i].max_level=ceil(max_level);
			pci_set[j][i].table_memory=table_bit;
			pci_set[j][i].wb_memory=WB;
			double helper_memory=0;
#if 1
			for(uint32_t i=1; i<=ceil(max_level); i++){
				uint64_t level_size;
				level_size=buffered_ent * ceil(pow(size_factor, i));
				total_level_size+=level_size;
			}
			double BF_memory_bit=0;
			double PLR_memory_bit=0;
			double PLR_level_size=0;
			double BF_level_size=0;
			double BF_ratio=0;
			for(uint32_t b=ceil(max_level); b>=1; b--){
				double level_size;
				double run_size=buffered_ent * pow(size_factor, b-1);	

				if(b==max_level){
					level_size=buffered_ent * ceil(pow(size_factor, b));
				}
				else{
					level_size=buffered_ent * ceil(pow(size_factor, b));
				}

/*
				level_size=level_size>RANGE?RANGE:level_size;
				run_size=run_size>RANGE?RANGE:run_size;
*/
				double run_coverage_ratio=(double)run_size/total_level_size;
				if(bf_memory_per_ent(run_coverage_ratio) < plr_memory_per_ent(run_coverage_ratio)){
					BF_memory_bit+=bf_memory_per_ent(run_coverage_ratio)*level_size;
					BF_level_size+=level_size;
				}
				else{
					PLR_memory_bit+=plr_memory_per_ent(run_coverage_ratio)*level_size;
					PLR_level_size+=level_size;
				}
				helper_memory+=level_size * 
					MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
			}
			pci_set[j][i].total_level_size=total_level_size;
			double adjust_helper_memory=(helper_memory/total_level_size*RANGE);
			memory_size+=adjust_helper_memory;
			pci_set[j][i].bf_memory=adjust_helper_memory*(BF_level_size/total_level_size);
			pci_set[j][i].plr_memory=adjust_helper_memory*(PLR_level_size/total_level_size);

#else
			double average_run_size=RANGE/(max_level*size_factor);
			double bit_per_ent=0;
			if(bf_memory_per_ent(average_run_size/RANGE) < plr_memory_per_ent(average_run_size/RANGE)){
				bit_per_ent=bf_memory_per_ent(average_run_size/RANGE);
			}
			else{
				bit_per_ent=plr_memory_per_ent(average_run_size/RANGE);
			}
			helper_memory=bit_per_ent*RANGE;

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

			uint64_t bf_level_size=0;
			uint64_t plr_level_size=0;
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
				double bf_memory=bf_memory_per_ent((double)run_size/total_level_size);
				double plr_memory=plr_memory_per_ent((double)run_size/total_level_size);
				if(bf_memory < plr_memory){
					bf_level_size+=level_size;
				}
				else{
					plr_level_size+=level_size;
				}
			}
			memory_size+=helper_memory;
			pci_set[j][i].bf_memory=helper_memory * ((double)bf_level_size/total_level_size);
			pci_set[j][i].plr_memory=helper_memory * ((double)plr_level_size/total_level_size);
#endif
			pci_set[j][i].total_memory=memory_size;
			printf("%.3lf\t", memory_size/(RANGE*48));
	//		if(max_level==1) break;
		}
		printf("\n");
	}

	printf("\nWAF---\n");
	printf("\t\t");
	for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor+=sf_multiple, i++){
		printf("%u\t", size_factor);
	}
	printf("\n");
	for(uint64_t WB=130092/*(LOWQDEPTH*(48+48))*/, j=0; WB<=RANGE*48 && j<limit_round; WB*=wb_multiple, j++){	
		double write_buffer_memory_bit=WB;
		double buffered_ent=write_buffer_memory_bit/(48+48);
		double chunk_num=(double)RANGE/buffered_ent;
		printf("%.0f\t",(float)WB/(48+48)*4/K);
		for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor+=sf_multiple, i++){
			printf("%u\t", pci_set[j][i].max_level);
		}
		printf("\n");
	}
	
	printf("\n");
	printf("memory_break down\n");
	printf("(L0,SF)  \t[L0]\t[BF]\t[PLR]\t[TLB]\n");
	for(uint32_t j=0; j<limit_round; j++){
		for(uint32_t i=0; i<limit_round; i++){
			printf("(%.0f,%u)  \t",	pci_set[i][j].wb_memory/(48+48)*4/K,	pci_set[i][j].size_factor);
			printf("%.3f\t",		pci_set[i][j].wb_memory /	pci_set[i][j].total_memory);
			printf("%.3f\t",		pci_set[i][j].bf_memory /	pci_set[i][j].total_memory);
			printf("%.3f\t",		pci_set[i][j].plr_memory /	pci_set[i][j].total_memory);
			printf("%.3f\t",		pci_set[i][j].table_memory/ pci_set[i][j].total_memory);
			printf("\n");
		}
		printf("\n");
	}

	printf("\n");
	for(uint32_t j=0; j<limit_round; j++){
		for(uint32_t i=0; i<limit_round; i++){	
			printf("(%.1f,%u)  \t",	pci_set[i][j].wb_memory/(48+48)*4/K,	pci_set[i][j].size_factor);
			printf("%.0lf\t",		pci_set[i][j].wb_memory);
			printf("%.0lf\t",		pci_set[i][j].bf_memory);
			printf("%.0lf\t",		pci_set[i][j].plr_memory);
			printf("%.0lf\t",		pci_set[i][j].table_memory);
			printf("%.0lf\t",		pci_set[i][j].total_level_size);
			printf("\n");
		}
		printf("\n");
	}
	printf("\n");

	printf("%.3f\n", pci_set[2][2].total_memory/(RANGE*48));
/*
	printf("(%.1f,%u)  \t",	pci_set[2][2].wb_memory/(48+48)*4/K,	pci_set[2][2].size_factor);
	printf("%.0lf\t",		pci_set[2][2].wb_memory);
	printf("%.0lf\t",		pci_set[2][2].bf_memory);
	printf("%.0lf\t",		pci_set[2][2].plr_memory);
	printf("%.0lf\t",		pci_set[2][2].table_memory);
	printf("\n");
*/
	for(uint32_t i=0; i<limit_round; i++){
		free(pci_set[i]);
	}
	free(pci_set);

}

#if 0
void lsmtree_tiering_memory_waf_calculator(uint32_t limit_round){
	uint32_t sf_multiple=2;
	uint32_t wb_multiple=50;
	//printf("BUF\tSF\tLEVEL\tTABLE\tBPE\tSUM\t\n");
	_pci **pci_set=(_pci**)calloc(limit_round, sizeof(_pci*));
	/*pci_set[L0-idx][SF-idx]*/
	for(uint32_t i=0; i<limit_round; i++){
		pci_set[i]=(_pci*)calloc(limit_round, sizeof(_pci));	
	}

	printf("memory usage---\n");
	printf("\t");
	for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor*=sf_multiple, i++){
		printf("%u\t", size_factor);
	}
	printf("\n");
	for(uint32_t WB=(LOWQDEPTH*(48+48)), j=0; WB<=RANGE*48 && j<limit_round; WB*=wb_multiple, j++){	
		double write_buffer_memory_bit=WB;
		double buffered_ent=write_buffer_memory_bit/(48+48);
		double chunk_num=(double)RANGE/buffered_ent;
		printf("%.2f\t",(float)WB/(48+48)*4/K);
		for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor*=sf_multiple, i++){
			double sum_plr_memory=0;
			double sum_bf_memory=0;

			uint64_t num_bf_entry=0;
			uint64_t num_plr_entry=0;

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
				if(bf_memory_per_ent(run_coverage_ratio) < plr_memory_per_ent(run_coverage_ratio)){
					sum_bf_memory+=level_size*bf_memory_per_ent(run_coverage_ratio);
					num_bf_entry+=level_size;
				}
				else{
					sum_plr_memory+=level_size*plr_memory_per_ent(run_coverage_ratio);
					num_plr_entry+=level_size;
				}
				plr_bit+=level_size * MIN(bf_memory_per_ent(run_coverage_ratio), plr_memory_per_ent(run_coverage_ratio));
			}

			double bit_per_entry=plr_bit/total_level_size;
			memory_size+=RANGE*bit_per_entry;
			
			pci_set[j][i].size_factor=size_factor;
			pci_set[j][i].max_level=max_level;
			pci_set[j][i].total_memory=memory_size;

			pci_set[j][i].bf_memory=sum_bf_memory/plr_bit*(RANGE*bit_per_entry);
			pci_set[j][i].plr_memory=sum_plr_memory/plr_bit*(RANGE*bit_per_entry);

			pci_set[j][i].table_memory=table_bit;
			pci_set[j][i].wb_memory=WB;

			printf("%.3lf\t", memory_size/(RANGE*48));
	//		printf("%.3lf\t", max_level);
	//		printf("%u\t%u\t%.3lf\t%.3lf\t%.3lf\t%.3lf\n",WB/LOWQDEPTH, size_factor, max_level, table_bit/(RANGE*48), 
	//				bit_per_entry/(48), memory_size/(RANGE*48));
			if(max_level==1) break;
		}
		printf("\n");
	}

	printf("\nWAF---\n");
	printf("\t\t");
	for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor*=sf_multiple, i++){
		printf("%u\t", size_factor);
	}
	printf("\n");
	for(uint32_t WB=(LOWQDEPTH*(48+48)), j=0; WB<=RANGE*48 && j<limit_round; WB*=wb_multiple, j++){	
		double write_buffer_memory_bit=WB;
		double buffered_ent=write_buffer_memory_bit/(48+48);
		double chunk_num=(double)RANGE/buffered_ent;
		printf("%u\t%.1f\t",WB/LOWQDEPTH, (float)WB/K);
		for(uint32_t size_factor=2, i=0; size_factor<=1024 && i<limit_round; size_factor*=sf_multiple, i++){
			printf("%u\t", pci_set[j][i].max_level);
		}
		printf("\n");
	}
	
	printf("\n");
	printf("memory_break down\n");
	printf("(L0,SF)  \t[L0]\t[BF]\t[PLR]\t[TLB]\n");
	for(uint32_t j=0; j<limit_round; j++){
		for(uint32_t i=0; i<limit_round; i++){
			printf("(%.1f,%u)  \t",	pci_set[i][j].wb_memory/(48+48)*4/K,	pci_set[i][j].size_factor);
			printf("%.3f\t",		pci_set[i][j].wb_memory /	pci_set[i][j].total_memory);
			printf("%.3f\t",		pci_set[i][j].bf_memory /	pci_set[i][j].total_memory);
			printf("%.3f\t",		pci_set[i][j].plr_memory /	pci_set[i][j].total_memory);
			printf("%.3f\t",		pci_set[i][j].table_memory/ pci_set[i][j].total_memory);
			printf("\n");
		}
		printf("\n");
	}

	printf("\n");
	for(uint32_t j=0; j<limit_round; j++){
		for(uint32_t i=0; i<limit_round; i++){
			printf("(%.1f,%u)  \t",	pci_set[i][j].wb_memory/(48+48)*4/K,	pci_set[i][j].size_factor);
			printf("%.0lf\t",		pci_set[i][j].wb_memory);
			printf("%.0lf\t",		pci_set[i][j].bf_memory);
			printf("%.0lf\t",		pci_set[i][j].plr_memory);
			printf("%.0lf\t",		pci_set[i][j].table_memory);
			printf("\n");
		}
		printf("\n");
	}

	for(uint32_t i=0; i<limit_round; i++){
		free(pci_set[i]);
	}
	free(pci_set);
}
#endif 
