/*
 *  drivers/cpufreq/cpufreq_ondemand.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include "cpufreq_governor.h"

//!!!!!!! cpufreq_spsa2.h is used!
#include "cpufreq_spsa2.h"
#include <linux/random.h>
#include "../../kernel/sched/sched.h"

/* On-demand governor macros */
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#ifdef CONFIG_ZEN_INTERACTIVE
#define DEF_SAMPLING_DOWN_FACTOR		(10)
#else
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#endif
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_UP_THRESHOLD		(95)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

extern unsigned long boosted_cpu_util(int cpu);
//extern unsigned long spsa_cpu_util_freq(int cpu);

static DEFINE_PER_CPU(struct od_cpu_dbs_info_s, od_cpu_dbs_info);

static struct od_ops od_ops;

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SPSA2TEST
static struct cpufreq_governor cpufreq_gov_spsa2_test;
#endif

static unsigned int default_powersave_bias;

static void spsa2_test_powersave_bias_init_cpu(int cpu)
{
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info, cpu);

    pr_warn("gov spsa2 call: spsa2_test_powersave_bias_init_cpu, cpu: %d \n", cpu);

	dbs_info->freq_table = cpufreq_frequency_get_table(cpu);
	dbs_info->freq_lo = 0;
}

/*
 * Not all CPUs want IO time to be accounted as busy; this depends on how
 * efficient idling at a higher frequency/voltage is.
 * Pavel Machek says this is not so for various generations of AMD and old
 * Intel systems.
 * Mike Chan (android.com) claims this is also not true for ARM.
 * Because of this, whitelist specific known (series) of CPUs by default, and
 * leave all others up to the user.
 */
static int should_io_be_busy(void)
{
#if defined(CONFIG_X86)
	/*
	 * For Intel, Core 2 (model 15) and later have an efficient idle.
	 */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
			boot_cpu_data.x86 == 6 &&
			boot_cpu_data.x86_model >= 15)
		return 1;
#endif
	return 0;
}

/*
 * Find right freq to be set now with powersave_bias on.
 * Returns the freq_hi to be used right now and will set freq_hi_jiffies,
 * freq_lo, and freq_lo_jiffies in percpu area for averaging freqs.
 */
static unsigned int generic_powersave_bias_target(struct cpufreq_policy *policy,
		unsigned int freq_next, unsigned int relation)
{
	unsigned int freq_req, freq_reduc, freq_avg;
	unsigned int freq_hi, freq_lo;
	unsigned int index = 0;
	unsigned int jiffies_total, jiffies_hi, jiffies_lo;
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info,
						   policy->cpu);
	struct dbs_data *dbs_data = policy->governor_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;

	if (!dbs_info->freq_table) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_next;
	}

	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_next,
			relation, &index);
	freq_req = dbs_info->freq_table[index].frequency;
	freq_reduc = freq_req * od_tuners->powersave_bias / 1000;
	freq_avg = freq_req - freq_reduc;

	/* Find freq bounds for freq_avg in freq_table */
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_H, &index);
	freq_lo = dbs_info->freq_table[index].frequency;
	index = 0;
	cpufreq_frequency_table_target(policy, dbs_info->freq_table, freq_avg,
			CPUFREQ_RELATION_L, &index);
	freq_hi = dbs_info->freq_table[index].frequency;

	/* Find out how long we have to be in hi and lo freqs */
	if (freq_hi == freq_lo) {
		dbs_info->freq_lo = 0;
		dbs_info->freq_lo_jiffies = 0;
		return freq_lo;
	}
	jiffies_total = usecs_to_jiffies(od_tuners->sampling_rate);
	jiffies_hi = (freq_avg - freq_lo) * jiffies_total;
	jiffies_hi += ((freq_hi - freq_lo) / 2);
	jiffies_hi /= (freq_hi - freq_lo);
	jiffies_lo = jiffies_total - jiffies_hi;
	dbs_info->freq_lo = freq_lo;
	dbs_info->freq_lo_jiffies = jiffies_lo;
	dbs_info->freq_hi_jiffies = jiffies_hi;
	return freq_hi;
}

/*
 *spsa2 functions:
 */

#define FREQ_MAX_AMOUNT_1 13
#define FREQ_MAX_AMOUNT_2 21

static struct spsa2_policy_dbs_info clusters_data[2];

// bad numbers are 16, 15 - freqs amount, 6 - cpu cluster

 static unsigned int en_1[FREQ_MAX_AMOUNT_1] = {
    339366,
	339367, //39100,
	384615, //30361,
	446154, //28571,
	490716, //28574,
	536131, //29060,
	592516, //30212,
	656660, //31129,
	709402, //32603,
	792779, //34886,
	870827, //36433, // old values in commets from another smartphone
	951417, //37244, // power constant / frequency
	1052963  //38436 // all values was multiplied by 1000000 00000 (10^(10)) 
};

static unsigned int en_2[FREQ_MAX_AMOUNT_2] = {
    3159339,
    3159340,
	3159341, //73449,
	3353365, //73508,
	3547009, //76877,
	3759615, //79371,
	4073427, //83972,
	4294872, //89982,
	4637574, //99640,
	4869505, //107441,
	5224359, //115941,
	5594952, //125586,
	5848416, //134772,
	6372863, //137822,
	6649798, //144951,
	7067308, //151596,
	7353480, //155075,
	7941434, //158209,
    8398829, //1587000,
    8709936, //1588000,
    9350000, //1589000  // all values was multiplied by 1000000 00000 (10^(10))

};

static unsigned int freq_1[FREQ_MAX_AMOUNT_1] = {
    338000,    
    442000,
    546000,
    650000,
    754000,
    858000,
    962000,
    1066000, 
    1170000,
    1274000,
    1378000,
    1482000,
    1586000,
};

static unsigned int freq_2[FREQ_MAX_AMOUNT_2] = {
    520000,
    624000,    
    728000,
    832000,
    936000,
    1040000,
    1144000,
    1248000,
    1352000,
    1456000,
    1560000,
    1664000,
    1768000,
    1872000,
    1976000,
    2080000,
    2184000,
    2288000,
    2392000,
    2496000,
    2600000
};

static int get_freq_amount(unsigned int cluster)
{
    int freq_amount;
    freq_amount = cluster < 1 ? FREQ_MAX_AMOUNT_1 : FREQ_MAX_AMOUNT_2;
    
    if (cluster > 1)
        pr_warn("gov spsa2 <get_freq_amount> WRONG CLUSTER: %u \n", cluster);
    
    return freq_amount;	
}

static unsigned int* get_freq_array(unsigned int cluster)
{
    unsigned int* freq;
    freq = cluster < 1 ? freq_1 : freq_2;
    
    if (cluster > 1)
        pr_warn("gov spsa2 <get_freq_array> WRONG CLUSTER: %u \n", cluster);
    
    return freq;	
}

static unsigned int* get_en_array(unsigned int cluster)
{
    unsigned int* en;
    en = cluster < 1 ? en_1 : en_2;
    
    if (cluster > 1)
        pr_warn("gov spsa2 <get_en_array> WRONG CLUSTER: %u \n", cluster);
    
    return en;	
}

static signed int generate_delta(void)
{
    int rand;
    get_random_bytes(&rand, sizeof(rand));
	
    //pr_warn("gov spsa2, minus, random delta %d \n", rand);
    if (rand & 0x1)
    {
        //pr_warn("gov spsa2, minus, random delta %d \n", rand);
        return -1;
    }
    
    //pr_warn("gov spsa2, plus, random delta %d \n", rand);

    return 1;
}

static int find(unsigned int* freq, int freq_amount, unsigned int val)
{
    int i;
    for (i = 0; i < freq_amount; i++) {
        if (val == freq[i]) {
            return i;
        }
    }

    return -1;
}

static unsigned int find_closest(unsigned int frequency, unsigned int* freq, int freq_amount)
{
    unsigned int closest_ind;
    int i;

    closest_ind = 0;

    for (i = 0; i < freq_amount; i++) {
        if (abs((int)freq[i] - (int)frequency) < abs((int)frequency - (int)freq[closest_ind])) {
            closest_ind = i;
        }
    }

    return closest_ind;
}


static int calculate_functional(unsigned int current_load, int index, unsigned int* freq, unsigned int* en, int freq_amount, unsigned int target_load)
{
    unsigned int target_index = 0;
	unsigned int target_freq = 0;
	unsigned int current_freq = 0;
    unsigned int volume = 0;
    
    if (index < 0)
    {
            index = 0;
    }
    if (index > freq_amount - 1)
    {
            index = freq_amount - 1;
    }

    current_freq = freq[index];
	
	volume = current_freq * current_load;
	
	if(current_load <= target_load)
	{
		// no higher than target - trying to optimize consumption
		int min_index = freq_amount - 1;
		int i = 0;
		
		for(i = 0; i < freq_amount; i++) // searching amongst all frequencies for optimum
		{
			unsigned int projected_load = volume / freq[i];
			if(projected_load <= target_load && en[i] < en[min_index])
			{
				min_index = i;
			}
	    }
		
		target_freq = freq[min_index];
        target_index = min_index;
	}
	else
	{
		// higher than target => trying to meet the requirements
		target_index = find_closest(volume / target_load, freq, freq_amount);
        
        if ((current_load >= 100) && (target_index < freq_amount - 2))
        {
            target_index += 1;
        }

        target_freq = freq[target_index];
	}

	return abs(index - (int)target_index);
}

static int determine_new_freq(struct cpufreq_policy* policy, struct spsa2_policy_dbs_info* dbs_info, struct od_dbs_tuners *od_tuners, unsigned int cluster, unsigned int load)
{
	unsigned int* freq;
	//freq = policy->cpu < 6 ? freq_1 : freq_2;
	unsigned int* en;
	//en = policy->cpu < 6 ? en_1 : en_2;
	
	unsigned int current_freq;
	int index;

    int model;

	int new_index;

    unsigned int next_freq, target_load, log_print;
    
    int freq_amount;
    int betta;
    int alpha;
    // Have to add int alpha and int betta to <drivers/cpufreq/cpufreq_governor.h>, struct od_dbs_tuners
    alpha = od_tuners->alpha;
    betta = od_tuners->betta;
    target_load = od_tuners->target_load;
    log_print = od_tuners->log_print;

    //freq = policy->cpu < 4 ? freq_1 : freq_2;	
    //en = policy->cpu < 4 ? en_1 : en_2;
    //freq_amount = policy->cpu < 4 ? FREQ_MAX_AMOUNT_1 : FREQ_MAX_AMOUNT_2;
    
    freq = get_freq_array(cluster);
    en = get_en_array(cluster);
    freq_amount = get_freq_amount(cluster);
        
    current_freq = policy->cur;
    
    index = find(freq, freq_amount, current_freq);

    if(index < 0 || index > freq_amount - 1)
	{
        if(log_print)
		    pr_warn("gov spsa2, frequency %u is not found for cpu %u \n", current_freq, policy->cpu);
		index = 0;
	}
	
//	if(dbs_info->requested_freq != 0 && dbs_info->requested_freq != current_freq)
//	{
//        int index;
//		// we wait CPU to switch
//		//dbs_info->load_sum += load;
//		//dbs_info->load_count += 1;
//
//        //TODO may have trash in dbs_info->requested_freq
//        for(index = 0; index < freq_amount; index++)
//        {
//            if (dbs_info->requested_freq == freq[index])
//            {
//                pr_warn("gov spsa2, waiting cpu %u to switch from %u to %u \n", policy->cpu, current_freq, dbs_info->requested_freq);
//		        return dbs_info->requested_freq;
//            }
//        }
//
//		pr_warn("gov spsa2, waiting cpu %u to switch from %u to %u, but it is wrong freq (set %u freq as target) \n", policy->cpu, current_freq, dbs_info->requested_freq, freq[freq_amount / 2]);
//        dbs_info->requested_freq = freq[freq_amount / 2]; 		
//        return dbs_info->requested_freq;
//	}
	
    model = 0; //calculate_functional(load, index, freq, en, freq_amount, target_load);
    new_index = index;
    //////////////////////////////03.02.2023
    /// test 1 - only phase 0, calculate to functionals
    /// test 2 - 3 phase:
    ///// 0 - calculate minus  (and run freq)
    ///// 1 - calculate plus   (and run freq)
    ///// 2 - calculate gradient from prev 2 calcs and run the freq obtained
 
    if (dbs_info->old_index < 0 || dbs_info->old_index > (freq_amount - 1))
        dbs_info->old_index = index;
    
    if (dbs_info->phase == 0)
    {
        int i, delta, plus_model, minus_model, difference, gradient;
        int inds[2];
        unsigned int volume;
        volume = current_freq * load;
        
        delta = generate_delta();
        
        inds[0] = index + delta * betta;
        inds[1] = index - delta * betta;
        for (i = 0; i < 2; ++i)
        {
            if (inds[i] >= freq_amount)
                inds[i] = freq_amount - 1;
            
            if (inds[i] < 0)
                inds[i] = 0;
        }
    
        plus_model = calculate_functional(volume / freq[inds[0]], inds[0], freq, en, freq_amount, target_load);
        minus_model = calculate_functional(volume / freq[inds[1]], inds[1], freq, en, freq_amount, target_load);
        
        difference = plus_model - minus_model;
        if (abs(difference) == 1)
        {
            difference *= 2;
        }

        if ((load > target_load) && (difference == 0))
        {
            difference = -2;
            delta = 1;
        }

        gradient = (alpha * (difference)) / (2 * delta * betta);
        
        new_index = dbs_info->old_index - gradient;
        
        if (log_print)
            pr_warn("gov_spsa2, result cpu %u -- load: %u, t_load: %u,  plus: %d, minus: %d, al: %d, bt: %d, del: %d, grad: %d, old_i: %d, new_i: %d\n", policy->cpu, load, target_load, plus_model, minus_model, alpha, betta, delta, gradient, dbs_info->old_index, new_index);

        if (new_index < 0)
        {
            //pr_warn("gov_spsa2, new index < 0, cpu: %d \n", policy->cpu);
            new_index = 0;
        }
        
        if (new_index >= freq_amount)
        {
            //pr_warn("gov_spsa2, new index > %d, cpu: %d \n", freq_amount, policy->cpu);
            new_index = freq_amount - 1;
        }
    }
    else
    {
        dbs_info->phase = 0;
    }
    
    //pr_warn("gov_spsa2, results for cpu %u -- alpha: %d, betta: %d, old freq: %u, target freq: %u \n", policy->cpu, alpha, betta, current_freq, freq[new_index]);

    dbs_info->old_index = new_index;
    next_freq = freq[new_index];
    dbs_info->requested_freq = next_freq;
    
    return next_freq;
}
 /*
 *spsa2 functions ended
 */

///////////////////////////////////////////////////////////////////////////////////////////////
static void dbs_freq_increase(struct cpufreq_policy *policy, unsigned int freq)
{
	struct dbs_data *dbs_data = policy->governor_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;

    pr_warn("gov spsa2 BAD <dbs_freq_increase> HAVE TO BE NOT USED. freq changed to: %u", freq);

	if (od_tuners->powersave_bias)
		freq = od_ops.powersave_bias_target(policy, freq,
				CPUFREQ_RELATION_H);
	else if (policy->cur == policy->max)
		return;

	__cpufreq_driver_target(policy, freq, od_tuners->powersave_bias ?
			CPUFREQ_RELATION_L : CPUFREQ_RELATION_H);
}

/*
 * Every sampling_rate, we check, if current idle time is less than 20%
 * (default), then we try to increase frequency. Else, we adjust the frequency
 * proportional to load.
 *
 * od_update() - in default
 */
static void od_check_cpu(int cpu, unsigned int load)
{
    unsigned int freq_next, cluster, i;
    unsigned long utils[4], sutils[4], max_util, min_capas;
    unsigned int precent_util;

    struct spsa2_policy_dbs_info* dbs_info_spsa;

	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(od_cpu_dbs_info, cpu);
	struct cpufreq_policy *policy = dbs_info->cdbs.cur_policy;
	struct dbs_data *dbs_data = policy->governor_data;
	
    // alpha and betta params are here
    struct od_dbs_tuners *od_tuners = dbs_data->tuners;

	dbs_info->freq_lo = 0;
    
    // spsa2 functions use
    cluster = policy->cpu < 4 ? 0 : 1;   
    dbs_info_spsa = &(clusters_data[cluster]);

    //pr_warn("gov spsa2 call: od_check_cpu() cpu: %d, %u \n", cpu, load);
    
    max_util = 0;
    min_capas = 999999;    

    for (i = 0; i < 4; ++i)
    {
        utils[i] = boosted_cpu_util(i + cluster * 4);
        //sutils[i] = spsa_capacity_of(i);
        sutils[i] = arch_scale_cpu_capacity(NULL, i + cluster * 4);
        
        if (utils[i] > max_util)
            max_util = utils[i];
        
        if (sutils[i] < min_capas)
            min_capas = sutils[i];
    }   

    precent_util = (max_util * 100) / min_capas;

    if (od_tuners->log_print)
    {    
        pr_warn("gov spsa2 cpu: %d, load: %u, utils: c0: %lu, c1: %lu, c2: %lu, c3: %lu, precent: %u, max_ut: %lu, min_cap: %lu \n", cpu, load, utils[0], utils[1], utils[2], utils[3], precent_util, max_util, min_capas);

        pr_warn("gov spsa2 cpu: %d, load: %u, archc: c0: %lu, c1: %lu, c2: %lu, c3: %lu \n", cpu, load, sutils[0], sutils[1], sutils[2], sutils[3]);
	}
    
    

	freq_next = determine_new_freq(policy, dbs_info_spsa, dbs_data->tuners, cluster, precent_util);
	
	__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
    
    //pr_warn("gov_spsa2 od_check_cpu DO NOTHING cpu DONE: %d \n", cpu);
   
    // spsa2 functions use end    
    return;
    	
    /* Check for frequency increase */
	if (load > od_tuners->up_threshold) {
		/* If switching to max speed, apply sampling_down_factor */
		if (policy->cur < policy->max)
			dbs_info->rate_mult =
				od_tuners->sampling_down_factor;
		dbs_freq_increase(policy, policy->max);
	} else {
		/* Calculate the next frequency proportional to load */
		unsigned int freq_next, min_f, max_f;

		min_f = policy->cpuinfo.min_freq;
		max_f = policy->cpuinfo.max_freq;
		freq_next = min_f + load * (max_f - min_f) / 100;

		/* No longer fully busy, reset rate_mult */
		dbs_info->rate_mult = 1;

		if (!od_tuners->powersave_bias) {
			__cpufreq_driver_target(policy, freq_next,
					CPUFREQ_RELATION_C);
			return;
		}

		freq_next = od_ops.powersave_bias_target(policy, freq_next,
					CPUFREQ_RELATION_L);
		__cpufreq_driver_target(policy, freq_next, CPUFREQ_RELATION_C);
	}
}

static void od_dbs_timer(struct work_struct *work)
{
	struct od_cpu_dbs_info_s *dbs_info =
		container_of(work, struct od_cpu_dbs_info_s, cdbs.work.work);
	unsigned int cpu = dbs_info->cdbs.cur_policy->cpu;
	struct od_cpu_dbs_info_s *core_dbs_info = &per_cpu(od_cpu_dbs_info,
			cpu);
	struct dbs_data *dbs_data = dbs_info->cdbs.cur_policy->governor_data;
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	int delay = 0, sample_type = core_dbs_info->sample_type;
	bool modify_all = true;

    //pr_warn("gov spsa2 BAD <od_dbs_timer> USED\n");

	mutex_lock(&core_dbs_info->cdbs.timer_mutex);
	if (!need_load_eval(&core_dbs_info->cdbs, od_tuners->sampling_rate)) {
		modify_all = false;
		goto max_delay;
	}

	/* Common NORMAL_SAMPLE setup */
	core_dbs_info->sample_type = OD_NORMAL_SAMPLE;
	if (sample_type == OD_SUB_SAMPLE) {
		delay = core_dbs_info->freq_lo_jiffies;
		__cpufreq_driver_target(core_dbs_info->cdbs.cur_policy,
				core_dbs_info->freq_lo, CPUFREQ_RELATION_H);
	} else {
		dbs_check_cpu(dbs_data, cpu);
		if (core_dbs_info->freq_lo) {
			/* Setup timer for SUB_SAMPLE */
			core_dbs_info->sample_type = OD_SUB_SAMPLE;
			delay = core_dbs_info->freq_hi_jiffies;
		}
	}

max_delay:
	if (!delay)
		delay = delay_for_sampling_rate(od_tuners->sampling_rate
				* core_dbs_info->rate_mult);

	gov_queue_work(dbs_data, dbs_info->cdbs.cur_policy, delay, modify_all);
	mutex_unlock(&core_dbs_info->cdbs.timer_mutex);
}

/************************** sysfs interface ************************/
static struct common_dbs_data od_dbs_cdata;


static ssize_t store_betta(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
    pr_warn("gov spsa2 got value: %d, store_betta \n", input);
	
    if (ret != 1)
		return -EINVAL;
	od_tuners->betta = input;

	return count;
}

static ssize_t store_alpha(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
    pr_warn("gov spsa2 got value: %d, store_alpha \n", input);
	
    if (ret != 1)
		return -EINVAL;
	od_tuners->alpha = input;

	return count;
}

static ssize_t store_target_load(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
    pr_warn("gov spsa2 got value: %u, store_target_load \n", input);
	
    if (ret != 1)
		return -EINVAL;
	od_tuners->target_load = input;

	return count;
}

static ssize_t store_log_print(struct dbs_data *dbs_data, const char *buf,
		size_t count)
{
	struct od_dbs_tuners *od_tuners = dbs_data->tuners;
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
    pr_warn("gov spsa2 got value: %u, store_log_print \n", input);
	
    if (ret != 1)
		return -EINVAL;
	od_tuners->log_print = !!input;

	return count;
}

// Have to add int alpha and int betta to <drivers/cpufreq/cpufreq_governor.h>, struct od_dbs_tuners
show_store_one(od, betta);
show_store_one(od, alpha);
show_store_one(od, target_load);
show_store_one(od, log_print);


gov_sys_pol_attr_rw(betta);
gov_sys_pol_attr_rw(alpha);
gov_sys_pol_attr_rw(target_load);
gov_sys_pol_attr_rw(log_print);



static struct attribute *dbs_attributes_gov_sys[] = {
    &betta_gov_sys.attr,
    &alpha_gov_sys.attr,
    &target_load_gov_sys.attr,
    &log_print_gov_sys.attr,
	NULL
};

static struct attribute_group od_attr_group_gov_sys = {
	.attrs = dbs_attributes_gov_sys,
	.name = "spsa2_test",
};

static struct attribute *dbs_attributes_gov_pol[] = {
    &betta_gov_pol.attr,
    &alpha_gov_pol.attr,
    &target_load_gov_pol.attr,
    &log_print_gov_pol.attr,
	NULL
};

static struct attribute_group od_attr_group_gov_pol = {
	.attrs = dbs_attributes_gov_pol,
	.name = "spsa2_test",
};

/************************** sysfs end ************************/

static int od_init(struct dbs_data *dbs_data)
{
	struct od_dbs_tuners *tuners;
	u64 idle_time;
	int cpu;
    int cluster, freq_amount;
    unsigned int* freq;

	tuners = kzalloc(sizeof(*tuners), GFP_KERNEL);
	if (!tuners) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	cpu = get_cpu();
	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		tuners->up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		dbs_data->min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		tuners->up_threshold = DEF_FREQUENCY_UP_THRESHOLD;

		/* For correct statistics, we need 10 ticks for each measure */
		dbs_data->min_sampling_rate = MIN_SAMPLING_RATE_RATIO *
			jiffies_to_usecs(10);
	}

	tuners->sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR;
	tuners->ignore_nice_load = 0;
	tuners->powersave_bias = default_powersave_bias;
	tuners->io_is_busy = should_io_be_busy();

    // spsa2 functions use
    tuners->alpha = 2;
    tuners->betta = 1;
    tuners->target_load = 70;
    tuners->log_print = 0;
    // spsa2 functions use end

	dbs_data->tuners = tuners;
    
    // spsa2 functions use

    //clusters_data[cluster] = kmalloc(sizeof(struct spsa2_policy_dbs_info), GFP_KERNEL);
    
    for (cluster = 0; cluster < 2; cluster++)
    {   
        pr_warn("gov spsa2 init spsa2_test cluster: %d \n", cluster);
        freq = get_freq_array(cluster);    
        freq_amount = get_freq_amount(cluster);

        clusters_data[cluster].requested_freq = freq[freq_amount / 2];
        clusters_data[cluster].old_index = freq_amount / 2;
        clusters_data[cluster].delta = 1;
        clusters_data[cluster].phase = 0;
        clusters_data[cluster].old_load = 100;   
    }

    // spsa2 functions use end

	mutex_init(&dbs_data->mutex);
	return 0;
}

static void od_exit(struct dbs_data *dbs_data)
{
	kfree(dbs_data->tuners);
    pr_warn("gov spsa2 exit START \n");
}

define_get_cpu_dbs_routines(od_cpu_dbs_info);

static struct od_ops od_ops = {
	.powersave_bias_init_cpu = spsa2_test_powersave_bias_init_cpu,
	.powersave_bias_target = generic_powersave_bias_target,
	.freq_increase = dbs_freq_increase,
};

static struct common_dbs_data od_dbs_cdata = {
	.governor = GOV_ONDEMAND,
	.attr_group_gov_sys = &od_attr_group_gov_sys,
	.attr_group_gov_pol = &od_attr_group_gov_pol,
	.get_cpu_cdbs = get_cpu_cdbs,
	.get_cpu_dbs_info_s = get_cpu_dbs_info_s,
	.gov_dbs_timer = od_dbs_timer,
	.gov_check_cpu = od_check_cpu,
	.gov_ops = &od_ops,
	.init = od_init,
	.exit = od_exit,
};

static void od_set_powersave_bias(unsigned int powersave_bias)
{
	struct cpufreq_policy *policy;
	struct dbs_data *dbs_data;
	struct od_dbs_tuners *od_tuners;
	unsigned int cpu;
	cpumask_t done;

	default_powersave_bias = powersave_bias;
	cpumask_clear(&done);

	get_online_cpus();
	for_each_online_cpu(cpu) {
		if (cpumask_test_cpu(cpu, &done))
			continue;

		policy = per_cpu(od_cpu_dbs_info, cpu).cdbs.cur_policy;
		if (!policy)
			continue;

		cpumask_or(&done, &done, policy->cpus);

		if (policy->governor != &cpufreq_gov_spsa2_test)
			continue;

		dbs_data = policy->governor_data;
		od_tuners = dbs_data->tuners;
		od_tuners->powersave_bias = default_powersave_bias;
	}
	put_online_cpus();
}

void od_register_powersave_bias_handler_spsa2_test_copy(unsigned int (*f)
		(struct cpufreq_policy *, unsigned int, unsigned int),
		unsigned int powersave_bias)
{
	od_ops.powersave_bias_target = f;
	od_set_powersave_bias(powersave_bias);
}
EXPORT_SYMBOL_GPL(od_register_powersave_bias_handler_spsa2_test_copy);

void od_unregister_powersave_bias_handler_spsa2_test_copy(void)
{
	od_ops.powersave_bias_target = generic_powersave_bias_target;
	od_set_powersave_bias(0);
}
EXPORT_SYMBOL_GPL(od_unregister_powersave_bias_handler_spsa2_test_copy);

static int od_cpufreq_governor_dbs(struct cpufreq_policy *policy,
		unsigned int event)
{
	return cpufreq_governor_dbs(policy, &od_dbs_cdata, event);
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_SPSA2TEST
static
#endif
struct cpufreq_governor cpufreq_gov_spsa2_test = {
	.name			= "spsa2_test",
	.governor		= od_cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
    pr_warn("init cpufreq_g");
	return cpufreq_register_governor(&cpufreq_gov_spsa2_test);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
    pr_warn("spsa2_test exit cpufreq_gov_dbs_exit");
	cpufreq_unregister_governor(&cpufreq_gov_spsa2_test);
}

MODULE_AUTHOR("Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>");
MODULE_AUTHOR("Alexey Starikovskiy <alexey.y.starikovskiy@intel.com>");
MODULE_DESCRIPTION("'cpufreq_spsa2_test' - A dynamic cpufreq governor for "
	"Low Latency Frequency Transition capable processors");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SPSA2TEST
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
