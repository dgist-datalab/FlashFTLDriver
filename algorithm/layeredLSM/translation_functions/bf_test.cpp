#include "bf.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define MEMBERNUM 28
#define TESTMEMBERNUM 33

int main(){
	float target_fpr=0.1;
	float target_each_fpr=get_target_each_fpr(MEMBERNUM, target_fpr);
	uint32_t bit=get_number_of_bits(target_each_fpr);

	double result_fpr_sum=0;
	for(uint32_t i=1; i<=TESTMEMBERNUM-1; i++){
		result_fpr_sum+=1-pow(1-target_each_fpr,i-1);
	}

	printf("%f:%u %f %u %lf\n", target_fpr, MEMBERNUM, target_each_fpr, bit, result_fpr_sum/TESTMEMBERNUM);
}
