#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include "lsmtree_param_module.h"
#include "plr_memory_calculator.h"
#include "../../../include/settings.h"
#include "../../../include/utils/thpool.h"
#include "../translation_functions/bf_guard_mapping.h"
#define DIR_PATH "/home/kukania/BloomFTL-project/FlashFTLDriver/algorithm/layeredLSM/design_knob"

double *plr_dp;
double *line_per_chunk;
double *normal_plr_dp;
uint64_t *line_cnt_dp;
uint64_t *chunk_cnt_dp;

uint32_t global_error;

static uint32_t tt_target_bit;

static threadpool plr_make_th; 
void plr_func(void *arg, int __idx){
    const uint32_t range=5000000;
    uint32_t idx=*(uint32_t*)arg;
	if(idx==10){
	}
	printf("idx:%u!\n", idx);
    plr_dp[idx]=plr_memory_calc_avg((range/1000*idx),tt_target_bit, global_error, range, false, 
			&line_per_chunk[idx], &normal_plr_dp[idx], &line_cnt_dp[idx], &chunk_cnt_dp[idx]);
	free(arg);
}

static void write_data(int fd, char *data, uint32_t size_of, uint32_t cnt){
	uint32_t written_byte;
	if((written_byte=write(fd, data, size_of *cnt))!=-1){
		fsync(fd);
	}
	else{
		printf("%d temp:%d, sizeof():%u %p\n", fd, written_byte, size_of, data);
		perror("??\n");
	}
}

static void read_data(int fd, char *data, uint32_t size_of, uint32_t cnt){
	if(read(fd, data, cnt *size_of)){

	}else{
		printf("???\n");
	}
}

void init_memory_info(uint32_t error, uint32_t target_bit){
	tt_target_bit=target_bit;
	plr_dp=(double*)calloc(1001, sizeof(double));
	normal_plr_dp=(double*)calloc(1001, sizeof(double));
	line_per_chunk=(double*)calloc(1001, sizeof(double));
	line_cnt_dp=(uint64_t *)calloc(1001, sizeof(uint64_t));
	chunk_cnt_dp=(uint64_t *)calloc(1001, sizeof(uint64_t));

	char plr_table_map[512]={0,};
    sprintf(plr_table_map,"%s/plr_table/%u-%d.map", DIR_PATH, target_bit, error);
	int fd=open(plr_table_map, O_CREAT | O_RDWR, 0666);
	if(fd==-1){
		perror("????\n");
		exit(1);
	}
	bool new_file=false;
	if(lseek(fd, 0, SEEK_END)==0){
        new_file=true;
    }

	global_error=error;

    lseek(fd,0,SEEK_SET);
	if(new_file){
		printf("plr memory table does not exists\n");
		plr_make_th=thpool_init(4);
        plr_memory_calc_init(5000000);
        for(volatile uint32_t i=1; i<=1000; i++){          
			uint32_t *temp=(uint32_t*)malloc(sizeof(uint32_t));
			*temp=i;
            thpool_add_work(plr_make_th, plr_func, (void*)temp);
        }
        thpool_wait(plr_make_th);
//		free(temp);
		thpool_destroy(plr_make_th);

		write_data(fd, (char*)plr_dp, sizeof(double), 1001);
		write_data(fd, (char*)line_per_chunk, sizeof(double), 1001);
		write_data(fd, (char*)normal_plr_dp, sizeof(double), 1001);
		write_data(fd, (char*)line_cnt_dp, sizeof(uint64_t), 1001);
		write_data(fd, (char*)chunk_cnt_dp, sizeof(uint64_t), 1001);

	//	EPRINT("please add new table for fpr", true);
	//	exit(1);
	}
	else{
		read_data(fd,(char*) plr_dp, sizeof(double), 1001);
		read_data(fd,(char*) line_per_chunk, sizeof(double), 1001);
		read_data(fd,(char*) normal_plr_dp, sizeof(double), 1001);
		read_data(fd,(char*) line_cnt_dp, sizeof(uint64_t), 1001);
		read_data(fd,(char*) chunk_cnt_dp, sizeof(uint64_t), 1001);
	}

/*
	printf("opt memory usage\n");
	for(uint32_t i=1; i<=1000; i++){
		printf("%u\t%lf\n", i, plr_dp[i]);
	}*/
	/*
	printf("normal memory usage\n");
	for(uint32_t i=1; i<=1000; i++){
		printf("%u\t%lf\n", i, normal_plr_dp[i]);
	}
	*/
	find_sub_member_num((float)global_error/100, 10000, target_bit);
	close(fd);
}

double bf_memory_per_ent(double ratio, uint32_t target_bit){
	return find_sub_member_num((float)global_error/100, 10000, target_bit);
}

double plr_memory_per_ent(double ratio, uint32_t target_bit){
	uint32_t temp=ratio*1000;
	if(temp==0) 
		return tt_target_bit;
	return plr_dp[temp];
}

double get_line_per_chunk(double ratio){
	uint32_t temp=ratio*1000;
	if(temp==0) 
		return 48;
	return line_per_chunk[temp];
}

void destroy_memory_info(){
	free(plr_dp);
}

double bf_advance_ratio(uint32_t target_bit){
	double bf_avg_bit=find_sub_member_num((float)global_error/100, 10000, target_bit);
	uint32_t target_idx;
	for(uint32_t i=1; i<1000; i++){
		if(plr_dp[i] < bf_avg_bit){
			break;
		}
		else{
			target_idx=i;
		}
	}
	return (double)target_idx/1000;
}

uint64_t line_cnt(double ratio){
	uint32_t temp=ratio*1000;
	if(temp==0){
		return 0;
	}
	return line_cnt_dp[temp];
}

uint64_t chunk_cnt(double ratio){
	uint32_t temp=ratio*1000;
	if(temp==0){
		return 0;
	}
	return chunk_cnt_dp[temp];
}
