#include "cpufreq_governor.h"


struct spsa2_policy_dbs_info
{
	/*
	policy_dbs_info ->  cpufreq_policy but od_cpu_dbs_info_s, or dbs_data 
	*/
    //struct dbs_data policy_dbs;

    //unsigned int freq_lo;
    //unsigned int freq_lo_delay_us;
    //unsigned int freq_hi_delay_us;

    signed int delta;
	int direction;
    signed int old_index;
    unsigned int phase;
	unsigned int old_load;
	unsigned int requested_freq;
	unsigned int load_sum;
	unsigned int load_count;
    
    int plus_model;
    int minus_model;

    unsigned int sample_type:1;
};

