#include "../include/settings.h"
#include "./bench.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <random>
int zipf(double alpha, int n)
{
  static int first = true;      // Static first time flag
  static double c = 0;          // Normalization constant
  static double *sum_probs;     // Pre-calculated sum of probabilities
  double z;                     // Uniform random number (0 < z < 1)
  int zipf_value;               // Computed exponential value to be returned
  int    i;                     // Loop counter
  int low, high, mid;           // Binary-search bounds

  // Compute normalization constant on first call only
  if (first == true)
  {
    for (i=1; i<=n; i++)
      c = c + (1.0 / pow((double) i, alpha));
    c = 1.0 / c;

    sum_probs = (double*)malloc((n+1)*sizeof(*sum_probs));
    sum_probs[0] = 0;
    for (i=1; i<=n; i++) {
      sum_probs[i] = sum_probs[i-1] + c / pow((double) i, alpha);
    }
    first = false;
  }

  // Pull a uniform random number (0 < z < 1)
  do
  {
    z = (float)rand()/(float)(RAND_MAX);
  }
  while ((z == 0) || (z == 1));

  // Map z to the value
  low = 1;
  high = n;
  do {
    mid = floor((low+high)/2);
    if (sum_probs[mid] >= z && sum_probs[mid-1] < z) {
      zipf_value = mid;
      break;
    } else if (sum_probs[mid] >= z) {
      high = mid-1;
    } else {
      low = mid+1;
    }
  } while (low <= high);

  // Assert that zipf_value is between 1 and N
  assert((zipf_value >=1) && (zipf_value <= n));

  return(zipf_value);
}

extern master *_master;
void vectored_localized_get(uint32_t start, uint32_t end, monitor *m){
	uint32_t request_per_command=_master->trans_configure.request_num_per_command;
	uint32_t number_of_command=(m->m_num)/request_per_command;
	m->m_num=number_of_command*request_per_command;
    m->tbody=(transaction_bench_value*)malloc(number_of_command * sizeof(transaction_bench_value));

	uint32_t request_buf_size=_master->trans_configure.request_size * request_per_command;
	m->command_num=number_of_command;
	m->command_issue_num=0;
    for(uint32_t i=0; i<number_of_command; i++){
		uint32_t idx=0;
		m->tbody[i].buf=(char*)malloc(request_buf_size + TXNHEADERSIZE);
		char *buf=m->tbody[i].buf;

		idx+=sizeof(uint32_t);//tid
		(*(uint32_t*)&buf[idx])=request_per_command;
		idx+=sizeof(uint32_t);

		for(uint32_t j=0; j<request_per_command; j++){
			(*(uint8_t*)&buf[idx])=FS_GET_T;
			idx+=sizeof(uint8_t);
			(*(uint32_t*)&buf[idx])=zipf(0.7, end);
			idx+=sizeof(uint32_t);
			(*(uint32_t*)&buf[idx])=0; //offset
			idx+=sizeof(uint32_t);
			m->read_cnt++;
		}
	}
}