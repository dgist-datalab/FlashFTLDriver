#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include "lsmtree_param_module.h"
#include "plr_memory_calculator.h"
#include "bf_memory_calculator.h"
#include "../bloomfilter/guard_bf_set.h"
#include "thpool.h"
#define MIN(a,b) ((a)<(b)?(a):(b))

enum {
BF, PLR
};

static uint32_t ENTRYPERPAGE=0;
static uint32_t debug_lba=UINT32_MAX;
static double target_WAF[100+1];
volatile tlog_ tlog[100+1];
static uint32_t DEV_size;
static uint32_t LBA_size;
static uint32_t write_buffer_size;
static threadpool thpool;
bool WAF_STOP_FLAG;
int32_t fd;
double plr_dp[1001];

static void func(void *arg, int __idx){
	uint32_t idx=*(uint32_t*)arg;
	uint64_t memory_limit_bit=((double)DEV_size/100*idx)*48 + PAGE_TARGET_KILO*8;
	double min_WAF=DEV_size;
	uint32_t size_factor=UINT32_MAX;
	uint32_t prev_size_factor=UINT32_MAX;
	uint32_t level=1;
	bool stop_flag=false;
	
	if(idx==30){
		stop_flag=true;
	}
		//	tlog[idx].border=UINT32_MAX;
	for(uint32_t buffer_loop=1, now_write_buffer_size=write_buffer_size*buffer_loop;
			now_write_buffer_size*(48*2) < memory_limit_bit; 
			buffer_loop++, now_write_buffer_size=write_buffer_size*buffer_loop){
		prev_size_factor=0;
		if(idx==30 && buffer_loop>40){
//			printf("break!\n");
		}
		while(1){
			size_factor=get_size_factor(level, DEV_size/now_write_buffer_size);
			if(prev_size_factor==size_factor) break;
			for(uint32_t tiering_num=0; tiering_num<=level; tiering_num++){
				uint32_t border=UINT32_MAX;
				uint32_t leveling_num=level-tiering_num;
				uint64_t now_memory=0, bf_mem, plr_mem;
				now_memory+=ceil(log2(tiering_num*size_factor+(leveling_num)))*DEV_size; //memory for table
				now_memory+=now_write_buffer_size;
				if(now_memory > memory_limit_bit) break;
				uint32_t wiskey_num=0;
				for(wiskey_num=0; wiskey_num<=level; wiskey_num++)
				{
					double now_WAF=0;
					//now_WAF=tiering_num+(leveling_num)*size_factor;
					uint32_t cnt=1;
					for(uint32_t i=1; i<=leveling_num; i++){
						uint32_t entry_num=MIN(now_write_buffer_size*pow(size_factor, cnt), DEV_size);
						bf_mem=bf_memory_calc(entry_num, FPR, cnt<=wiskey_num);
						if(fd){
							uint32_t temp_idx=ceil((double)entry_num/DEV_size*1000);
							plr_mem=entry_num*(plr_dp[temp_idx]+(cnt<=wiskey_num?48:0));
						}
						else{
							plr_mem=plr_memory_calc(entry_num, FPR, DEV_size, cnt<=wiskey_num);

						}
						if(cnt<=wiskey_num){
							now_WAF+=(double)size_factor/ENTRYPERPAGE/2;
						}
						else{
							now_WAF+=size_factor/2;
						}
						if(bf_mem>plr_mem && border==UINT32_MAX){
							border=cnt;
						}
						now_memory+=MIN(bf_mem, plr_mem);
						cnt++;
					}
					//	fprintf(stderr, "\t[log] leveling done!\n");

					for(uint32_t i=1; i<=tiering_num; i++){
						uint32_t entry_num=MIN(write_buffer_size*pow(size_factor, cnt), DEV_size);
						bf_mem=bf_memory_calc(entry_num, FPR, cnt<=wiskey_num);
						if(fd){
							uint32_t temp_idx=ceil((double)entry_num/DEV_size*1000);
							plr_mem=entry_num*(plr_dp[temp_idx]+(cnt<=wiskey_num?48:0));
						}
						else{
							plr_mem=plr_memory_calc(entry_num, FPR, DEV_size, cnt<=wiskey_num);
						}

						now_memory+=MIN(bf_mem, plr_mem);
						if(cnt<=wiskey_num){
							if(cnt==level){
								double possibility=((double)1-(double)DEV_size/size_factor/LBA_size);
								uint32_t first=(double)DEV_size/size_factor*(1-pow(possibility,size_factor-1));
								uint32_t second=(double)DEV_size/size_factor*(1-pow(possibility,size_factor-2));
								now_WAF+=(double)(first+second)/DEV_size/size_factor/ENTRYPERPAGE;
							}
							else{
								now_WAF+=(double)1/ENTRYPERPAGE;
							}
						}
						else{
							if(cnt==level){
								double possibility=((double)1-(double)DEV_size/size_factor/LBA_size);
								uint32_t first=(double)DEV_size/size_factor*(1-pow(possibility,size_factor-1));
								uint32_t second=(double)DEV_size/size_factor*(1-pow(possibility,size_factor-2));
								now_WAF+=(double)(first+second)/DEV_size/size_factor;
							}
							else{
								now_WAF+=1;
							}
						}
						if(bf_mem>plr_mem && border==UINT32_MAX){
							border=cnt;
						}
						cnt++;
					}
					if(idx==30 && buffer_loop>40){
					//	printf("break!\n");
					}	
					if(wiskey_num!=level) now_WAF+=1;

					if(now_memory<=memory_limit_bit){
						if(min_WAF>now_WAF){
							min_WAF=now_WAF;
							tlog[idx].ln=leveling_num;			
							tlog[idx].tn=tiering_num;			
							tlog[idx].border=border;
							tlog[idx].sf=size_factor;
							tlog[idx].wiskey_line=wiskey_num;
							tlog[idx].buffer_multiple=buffer_loop;
						}
					}
				}
			}
			if(size_factor==2) break;
			level++;
			prev_size_factor=size_factor;
		}

	}
	//printf("done %u %.3lf %u\n", idx, min_WAF, tlog[idx].border);
	//printf("temp done %u %u\n", 1, target_WAF[1]);
	target_WAF[idx]=min_WAF;
	if(min_WAF==1) WAF_STOP_FLAG=true;
	free(arg);
}

static void plr_func(void *arg, int __idx){
	uint32_t idx=*(uint32_t*)arg;
	plr_dp[idx]=plr_memory_calc_avg((DEV_size/1000*idx), FPR, DEV_size, false);
}

design_knob design_knob_find_opt(uint32_t memory_target){
	LBA_size=RANGE;
	DEV_size=DEVFULL;
	ENTRYPERPAGE=(PAGE_TARGET_KILO/(6*2)); //sizeof(uint48_t) *2;
	write_buffer_size=ENTRYPERPAGE;
	gbf_set_prepare((float)FPR/100, 1000000, BLOOM_ONLY);
	
	bool new_file=false;
	char file_buf[100]={0,};
	sprintf(file_buf, "plr_memory_data/%u", LBA_size);
	fd=open(, O_CREAT | O_RDWR, 0666);
	if(fd==-1){
		perror("???\n");
		abort();
	}
	if(lseek(fd, 0, SEEK_END)==0){
		new_file=true;
	}
	lseek(fd,0,SEEK_SET);
	
	thpool=thpool_init(4);

	if(new_file){
		plr_memory_cacl_init(DEV_size);
		for(volatile uint32_t i=1; i<=1000; i++){
			uint32_t *temp=(uint32_t*)malloc(sizeof(uint32_t));
			*temp=i;
			thpool_add_work(thpool, plr_func, (void*)temp);
		}
		thpool_wait(thpool);
		int32_t temp;
		if((temp=write(fd, plr_dp ,sizeof(plr_dp)))!=-1){
			fsync(fd);
		}
		else{
			printf("%d temp:%d, sizeof():%lu %p\n", fd, temp, sizeof(plr_dp), plr_dp);
			perror("??\n");
			abort();
		}
	}
	else{
		if(read(fd, plr_dp, sizeof(plr_dp))){
		
		}else{
			printf("???\n");
			abort();
		}
	}
	
	uint32_t *memory=(uint32_t*)malloc(sizeof(uint32_t));
	*memory=memory_target;
	func((void*)memory, 0);

	close(fd);
	return tlog[memory_target];
}
