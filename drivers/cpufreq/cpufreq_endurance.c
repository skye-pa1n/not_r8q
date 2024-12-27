/*
 *  linux/drivers/cpufreq/cpufreq_endurance.c
 *
 *  Endurance CpuFreq Governor
 *
 *  Author: Tejas Udupa <tejasudupa1285@gmail>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; version 2
 *  of the License.
 *
 *
 *  Endurance V2:- Add support for idling CPU cores to lower idle frequency
 *		   and modify code for optimal use of idle frequency. Add idle_threshold
 *		   and idle Frequency as two more parameters tunabled from user space.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/thermal.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include "cpufreq_endurance.h"

unsigned int nap_time_ms = 1500;			// Governor sleep Timeout in millisecond
atomic_t nr_cpu_idle = ATOMIC_INIT(0);
/* governor status check variables */
unsigned int governor_enabled = 0;

ATOMIC_NOTIFIER_HEAD(therm_alert_notifier_head);
ATOMIC_NOTIFIER_HEAD(load_change_notifier_head);

static DEFINE_PER_CPU(struct cluster_prop *, cluster_nr);
static DEFINE_PER_CPU(struct per_cpu_info, cpuinfo);

static struct sensor_monitor *thermal_monitor;
static struct attribute_group *get_sysfs_attr(void);

/* realtime thread handles frequency scaling */
static struct task_struct *therm_mon_task;
static struct mutex gov_lock;
static struct mutex rw_lock;

/*		
 * get_cpufreq_table() initialises little and big core frequency tables.
 * does all reset/init process and any failure during this process leads
 * to governor disabling and not working for the failed cluster. 
 */
int get_cpufreq_table(struct cpufreq_policy *policy){
	
	struct cluster_prop *cluster;
	int ret = 0,i;
	
	/* If structure already initialized exit out */
	cluster = per_cpu(cluster_nr,policy->cpu);
	if(cluster)
		goto setup_done;
	
	/* Start cpu frequency table initialization */
	ret = init_cpufreq_table(policy);
	if(ret < 0)
		goto failed_inittbl;
		
	cluster = per_cpu(cluster_nr, policy->cpu);
	if(!cluster)
		goto failed_inittbl;
	if(!thermal_monitor)
		goto failed_inittbl;
		
	/* temprature initialization */
	ret = update_sensor_data();
	if(ret)
		goto failed_gettbl;
	
	// Debugging functions
	if(cfe_debug){
		for(i = 0; i <= cluster->nr_levels; i++)
			PDEBUG("%u ",cluster->freq_table[i].frequency);
		PDEBUG("\n");
	}
	
setup_done:
	/* initilse the cluster tunables */
	ret = init_tunables(policy);
	if(ret)
		goto failed_inittbl;
		
	/* reassign even if already inited as the address changes after resume/suspend */
	cluster->ppol = policy;	
	/* Cluster set initial frequency mitigation settings and parameters 
	 * before handing over to speedchange_task() thread for rest of cpu governing tasks.
	 */
	
	cfe_reset_params(policy);
	governor_enabled++;
	cluster->gov_enabled = 1;
	PDEBUG("governor state:%d",governor_enabled);
	return ret;

failed_inittbl:
	pr_err(KERN_WARNING"%s: Failed to initialise governor for core:%d\terr=%d", __func__,policy->cpu,ret);
failed_gettbl:
	return ret;
}

/*
 * init_cpufreq_table() initilizeses the the cluster_nr structure 
 * and assigns it to all cores of that cluster and initlises few structure members.
 * 
 * @nr_levels: total number of levels or highest index of frequency array
 * @freq_table: holds pointer to frequency table data 
 * @index: get the number of frequencies the respective cluster has
 * @ppol: holds the pointer to policy cluster of the respective cluster
 */
int init_cpufreq_table(struct cpufreq_policy *policy)
{
	struct cluster_prop *cluster = per_cpu(cluster_nr,policy->cpu);
	struct cpufreq_frequency_table *freq_table;
	int i;

	/* Get Highest Frequency Index in array */
	freq_table = cpufreq_frequency_get_table(policy->cpu);

	/* Initialise the cluster_prop structure. */
	if(!cluster){
		cluster = kzalloc(sizeof(struct cluster_prop), 
						GFP_KERNEL);
		if(!cluster)
			return -ENOMEM;

		memset(cluster, 0, sizeof(struct cluster_prop));
	}
	if(!thermal_monitor){
		thermal_monitor = kzalloc(sizeof(struct sensor_monitor), 
						GFP_KERNEL);
		if(!thermal_monitor)
			return -ENOMEM;

		memset(thermal_monitor, 0, sizeof(struct sensor_monitor));
	}
	
	/* assign cluster -> cluster_nr for each avilable core in that cluster */
	for_each_cpu(i, policy->related_cpus)
		per_cpu(cluster_nr,i) = cluster;
		
	cluster->freq_table = freq_table;

	return 0;
}

/*
 * init_tunables() initlisies the governor tunables to their initial
 * default values. sets up throttle temprature and temprature diffrence to be
 * kept for each cluster. The function is re-called everytime governor is
 * restarted as the tunable structure is re-inited with default values.
 */
int init_tunables(struct cpufreq_policy *policy)
{
	struct cluster_prop *cluster = per_cpu(cluster_nr,policy->cpu);
	struct cluster_tunables *tunable = cluster->cached_tunables;
	int rc = 0;
	
	if(!tunable){
		tunable = kzalloc(sizeof(struct cluster_tunables), 
						GFP_KERNEL);
		if(!tunable)
			return -ENOMEM;

		memset(tunable, 0, sizeof(struct cluster_tunables));
	}
	else
		goto end;

	/* Initialise throttle temperature of big and little cluster */
	if(policy->cpu <= NR_LITTLE){
		tunable->throttle_temperature = THROTTLE_TEMP_LITTLE;
		tunable->temperature_diff = TEMP_DIFF_LITTLE;
		tunable->max_throttle_step = MAX_STEP_LITTLE;
		tunable->idle_threshold = L_IDLE_TRESH;
	}
	else if(policy->cpu <= NR_BIG){
		tunable->throttle_temperature = THROTTLE_TEMP_BIG;
		tunable->temperature_diff = TEMP_DIFF_BIG;
		tunable->max_throttle_step = MAX_STEP_BIG;
		tunable->idle_threshold = B_IDLE_TRESH;
	}
	tunable->idle_frequency = cluster->freq_table[0].frequency;
	
end:
	cluster->cached_tunables = tunable;
	rc = sysfs_create_group(get_governor_parent_kobj(policy), 
								get_sysfs_attr());
	if (rc) {
		pr_err("%s: couldn't create sysfs attributes: %d\n", __func__, rc);
		goto err;
	}
	/* holds refrence of tunable structure per-policy so it
	 * it can be retrived for use later in show/store routines
	 */
	policy->governor_data = tunable;
	return 0;
err:
	policy->governor_data = NULL;
	return -ENOMEM;
}

/*
 * cfe_reset_params() resets the cluster max_freq and nr_levels
 * this helps us to do correct thermal mitigation and dynamically switch between
 * frequency and allows users to have a certain degree of control on the max frequency
 * which has direct impact on how the thermal mitigation proceeds.
 */
void cfe_reset_params(struct cpufreq_policy *policy)
{
	struct cluster_prop *cluster = per_cpu(cluster_nr, policy->cpu);
	int index = 0;

	if(!cluster)
		return;
 	
 	mutex_lock(&rw_lock);
 		
	/* policy limits change gets called multiple times eventhough change
	 * happened only once so therefore add checks to see if the reset
	 * has already been performed.
	 */
	if(cluster->max_freq == policy->max)
		goto skip;

	PDEBUG("Prev freq:%u Cur Freq:%u New freq:%u",cluster->max_freq,
					policy->cur, policy->max);
	/* using index navigate to the equavalent frequency as that of
	 * policy->max in our frequency table.
	 */
	while(cluster->freq_table[index].frequency < policy->max)
		index++;
	PDEBUG("Reseting Index to:%d",index);

	cluster->cur_level = 0;
	cluster->nr_levels = index;
	cluster->max_freq = cluster->prev_freq = policy->max;
skip:
	mutex_unlock(&rw_lock);
}
/*
 * update_sensor_data() get temperature reading from the desired sensor.
 * in case of failure to get current temperature from sensor it sets cur_temps to 0.
 */
static inline int update_sensor_data(void)
{
	int ret = 0;

	ret = sensor_get_temp(SENSOR_ID,&thermal_monitor->cur_temps);
	if(ret){
		pr_err(KERN_WARNING"%s: Failed to get sensor: %d temprature data.\n",
								__func__, SENSOR_ID);
		thermal_monitor->cur_temps = 0;
		return 1;
	}
	
	return 0;	
}

/*	
 * govern_cpu() calls for cpufreq mitigation based on temperature inputs from the respective sensor.
 * determines which frequency to throttle down/up to depending of the temperature of sensor.
 * precaution must be taken while passing cluster_prop as uninitilized variable ppol can
 * cause much pain(kernel panic!).
 */
static void govern_cpu(struct cluster_prop *cluster)
{
	struct cpufreq_policy *policy = cluster->ppol;
	struct cluster_tunables *tunable = policy->governor_data;
	int th_temp_diff = 0;
	int calc_temps = 0;
	
	if(!cluster->gov_enabled)
		return;

	th_temp_diff = thermal_monitor->cur_temps - tunable->throttle_temperature;
	PDEBUG("throt diff:%d cur_temp:%ld cpuid:%d", th_temp_diff, 
					thermal_monitor->cur_temps, policy->cpu);

	/* either we have not yet reached our cluster throttle temps or we dropped below
	 * throttle temps, so reset cluster levels and push max frequency of that cluster
	 */
	if(th_temp_diff < 0) {
		PDEBUG("Currrent temps Lower than throttle temps");
		if(cluster->nr_levels == cluster->cur_level)
			return;
		cluster->cur_level = cluster->nr_levels;
		do_cpufreq_mitigation(policy, cluster);
	}
	else {
		PDEBUG("Currrent temps Higher than throttle temps");
		calc_temps = cluster->nr_levels - 
				(th_temp_diff / tunable->temperature_diff + 1);
		/* exit if threshold was not reached or max throttle step was
		 * reached as we dont throttle down below user defined threshold
		 * to reduce performance impact due to further throttle.
		 */
		if((cluster->cur_level == calc_temps) ||
				(cluster->cur_level < tunable->max_throttle_step))
			goto end;
			
		cluster->cur_level = calc_temps;
		do_cpufreq_mitigation(policy, cluster);
	}
	return;

end:
	PDEBUG("No cpufreq changes required");
}

static int thermal_change_callback(struct notifier_block *nb, unsigned long val,
				void *data)
{
	unsigned int cpu = 0;
	mutex_lock(&rw_lock);

	/* loop through one core in each cluster */
	for_each_possible_cpu(cpu){
		if((cpu == NR_LITTLE) || (cpu == NR_BIG)){
			struct cluster_prop *cluster = per_cpu(cluster_nr, cpu);
			if(cluster)
				govern_cpu(cluster);
		}
	}
	mutex_unlock(&rw_lock);
	return 0;
}

static struct notifier_block therm_notifier_block = {
	.notifier_call = thermal_change_callback,
};

/*
 * update_load():- gives current load on the respective cpu core.
 */
static u64 update_load(int cpu)
{
	struct per_cpu_info *pcpu = &per_cpu(cpuinfo, cpu);
	u64 cur_wall_time;
	u64 cur_idle_time;
	int io_busy = 0;
	unsigned int wall_time, idle_time, load = 0;

	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, io_busy);
	wall_time = (unsigned int)
			(cur_wall_time - pcpu->prev_cpu_wall);
	idle_time = (unsigned int)
			(cur_idle_time - pcpu->prev_cpu_idle);

	pcpu->prev_cpu_wall = cur_wall_time;
	pcpu->prev_cpu_idle = cur_idle_time;
	load = 100 * (wall_time - idle_time) / wall_time;

	return load;
}

/*
 * idle_threshold_check():-
 * @threshold:- stores idle threshold, if +ve then cores are idle.
 * Checks if the average load on the cluster of cores is less than threshold,
 * if lower then frequency is downshifted into idle frequency and sets
 * idle_loop(if both clusters idle) so that temprature governing is disabled
 * until load rises above threshold. the nap time is reduced so as
 * to reduce jank when there is a sudden load rise.
 */
static void idle_threshold_check(struct cluster_prop *cluster ,int load_avg)
{
	struct cpufreq_policy *policy = cluster->ppol;
	struct cluster_tunables *tunable = policy->governor_data;
	int threshold;

	threshold = tunable->idle_threshold - load_avg;
	if(threshold > 0){
		if(cluster->idle_cpu){
			if(policy->cur != tunable->idle_frequency)
				goto update;
			return;
		}
		nap_time_ms = 500;
		atomic_inc(&nr_cpu_idle);
		cluster->idle_cpu = 1;
	}
	else if(policy->cur == tunable->idle_frequency){
		cluster->idle_cpu = 0;
		if(atomic_dec_and_test(&nr_cpu_idle))
			nap_time_ms = 1500;
	}
update:
	do_cpufreq_mitigation(policy, cluster);
}

/*
 * load_change_callback():-
 * @core:- keeps count of the number of cores in cluster.
 * @load:- stores current reported load.
 * @load_avg:- stores respective cluster's average load.
 * This function responds to load change events triggered by the main
 * thread at regualr intervals and calculates the current load and
 * average load and invokes idle_threshold_check() for descision making.
 */
static int load_change_callback(struct notifier_block *nb, unsigned long val,
				void *data)
{
	unsigned int cpu = 0;
	int core = 0, load = 0, load_avg, temp;

	for_each_possible_cpu(cpu){
		struct cluster_prop *cluster = per_cpu(cluster_nr, cpu);
		if(cluster && cluster->gov_enabled){
			temp = update_load(cpu);
			PDEBUG("load:%d core %d", temp, cpu);
			if(temp >= 60)
				load += 400;
			load += temp;
			core++;
			if((cpu == NR_LITTLE) || (cpu == NR_BIG)){
				load_avg = load / core;
				idle_threshold_check(cluster, load_avg);
				core = load = 0;
			}
		}
	}

	return 0;
}

static struct notifier_block load_chg_notifier_block = {
	.notifier_call = load_change_callback,
};

/*
 * do_cpufreq_mitigation() it modifies the policy max frequency to the latest max
 * updated by the calling function and sets the frequency on the cluster.
 */
static inline int do_cpufreq_mitigation(struct cpufreq_policy *policy,
							struct cluster_prop *cluster)
{
	struct cluster_tunables *tunable = policy->governor_data;

	PDEBUG("cur_temps:%ld throt_temps:%d prev_temps:%ld",thermal_monitor->cur_temps,
			tunable->throttle_temperature, thermal_monitor->prev_temps);
	PDEBUG("THROTTLE to %u from %u level:%d max_lvl:%d cpu:%d",cluster->freq_table[cluster->cur_level].frequency,
			policy->cur,cluster->cur_level,cluster->nr_levels, policy->cpu);
	if(cluster->idle_cpu)
		policy->max = tunable->idle_frequency;
	else{
		PDEBUG("Out Of Idle\n");
		policy->max = cluster->freq_table[cluster->cur_level].frequency;
		cluster->prev_freq = policy->max;
	}
	__cpufreq_driver_target(policy, policy->max,
						CPUFREQ_RELATION_C);
	return 0;
}

/*
 * therm_mon_task() calls the govern_cpu() function.
 * if this function fails te governor is essentially dead, locks have been put to mitigate
 * contention issues that may arise during execution.
 */
static int cfe_thermal_monitor_task(void *data)
{
	int ret = 0;
	
	PDEBUG("CFEndurance Running...");
	while (!kthread_should_stop()) {
		set_current_state(TASK_RUNNING);
		/* sleep thread until notifier completes its task or if govenor disabled */
		if(!governor_enabled){
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}
		/* skip thermal checks if both clusters idle */
		if(atomic_read(&nr_cpu_idle) == CLUSTER_NR)
			goto skip;

		/* get updated thermal reading */
		ret = update_sensor_data();
		if(ret)
			goto skip;

		/* compare with updated temps to see if the current temps changed or not */
		if(thermal_monitor->cur_temps != thermal_monitor->prev_temps)
			atomic_notifier_call_chain(&therm_alert_notifier_head, 0,0);
		thermal_monitor->prev_temps = thermal_monitor->cur_temps;
skip:
		atomic_notifier_call_chain(&load_change_notifier_head,0,0);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(nap_time_ms));
	}
	
	return 0;
}

/************************** sysfs interface ************************/

static ssize_t show_throttle_temperature(struct cluster_tunables *tunable, char *buf)
{
	if(!tunable)
		return 0;

	return sprintf(buf, "%u\n", tunable->throttle_temperature);
}

static ssize_t store_throttle_temperature(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	struct cluster_tunables *tunable = policy->governor_data;
	unsigned int throttle_temperature;

 	if (kstrtouint(buf, 10, &throttle_temperature))
 		return -EINVAL;

 	tunable->throttle_temperature = throttle_temperature;
 	cfe_reset_params(policy);

 	return count;
}

static ssize_t show_temperature_diff(struct cluster_tunables *tunable, char *buf)
{
	if(!tunable)
		return 0;

	return sprintf(buf, "%u\n", tunable->temperature_diff);
}

static ssize_t store_temperature_diff(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	struct cluster_tunables *tunable = policy->governor_data;
	unsigned int temperature_diff;

 	if (kstrtouint(buf, 10, &temperature_diff))
 		return -EINVAL;

 	tunable->temperature_diff = temperature_diff;
 	cfe_reset_params(policy);

 	return count;
}

static ssize_t show_max_throttle_step(struct cluster_tunables *tunable, char *buf)
{
	if(!tunable)
		return 0;

	return sprintf(buf, "%u\n", tunable->max_throttle_step);
}

static ssize_t store_max_throttle_step(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	struct cluster_tunables *tunable = policy->governor_data;
	unsigned int max_throttle_step;

 	if (kstrtouint(buf, 10, &max_throttle_step))
 		return -EINVAL;

 	tunable->max_throttle_step = max_throttle_step;
 	cfe_reset_params(policy);

 	return count;
}

static ssize_t show_idle_frequency(struct cluster_tunables *tunable, char *buf)
{
	if(!tunable)
		return 0;

	return sprintf(buf, "%u\n", tunable->idle_frequency);
}

static ssize_t store_idle_frequency(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	struct cluster_tunables *tunable = policy->governor_data;
	unsigned int idle_frequency;

 	if (kstrtouint(buf, 10, &idle_frequency))
 		return -EINVAL;

 	tunable->idle_frequency = idle_frequency;

 	return count;
}

static ssize_t show_idle_threshold(struct cluster_tunables *tunable, char *buf)
{
	if(!tunable)
		return 0;

	return sprintf(buf, "%u\n", tunable->idle_threshold);
}

static ssize_t store_idle_threshold(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	struct cluster_tunables *tunable = policy->governor_data;
	unsigned int idle_threshold;

 	if (kstrtouint(buf, 10, &idle_threshold))
 		return -EINVAL;

 	tunable->idle_threshold = idle_threshold;

 	return count;
}

/*
 * Create show/store routines (pulled from interactive governor)
 * pol: One governor instance per struct cpufreq_policy
 */
#define show_gov_pol_sys(file_name)					\
static ssize_t show_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, char *buf)				\
{									\
	return show_##file_name(policy->governor_data, buf);		\
}

#define store_gov_pol_sys(file_name)					\
static ssize_t store_##file_name##_gov_pol				\
(struct cpufreq_policy *policy, const char *buf, size_t count)	\
{									\
	return store_##file_name(policy, buf, count);	\
}

#define show_store_gov_pol_sys(file_name)				\
show_gov_pol_sys(file_name);						\
store_gov_pol_sys(file_name)

show_store_gov_pol_sys(throttle_temperature);
show_store_gov_pol_sys(temperature_diff);
show_store_gov_pol_sys(max_throttle_step);
show_store_gov_pol_sys(idle_frequency);
show_store_gov_pol_sys(idle_threshold);

#define gov_pol_attr_rw(_name)					\
static struct freq_attr _name##_gov_pol =				\
__ATTR(_name, 0664, show_##_name##_gov_pol, store_##_name##_gov_pol)

#define gov_sys_pol_attr_rw(_name)					\
	gov_pol_attr_rw(_name)

gov_sys_pol_attr_rw(throttle_temperature);
gov_sys_pol_attr_rw(temperature_diff);
gov_sys_pol_attr_rw(max_throttle_step);
gov_sys_pol_attr_rw(idle_frequency);
gov_sys_pol_attr_rw(idle_threshold);

static struct attribute *edgov_attributes[] = {
	&throttle_temperature_gov_pol.attr,
	&temperature_diff_gov_pol.attr,
	&max_throttle_step_gov_pol.attr,
	&idle_frequency_gov_pol.attr,
	&idle_threshold_gov_pol.attr,
	NULL
};

static struct attribute_group edgov_tunables = {
	.attrs = edgov_attributes,
	.name = "endurance",
};

static struct attribute_group *get_sysfs_attr(void)
{
	return &edgov_tunables;
}

/************************** sysfs end ****************************/

/*
 * start_gov_setup() setups the governor.
 * setup up the frequencytable and all related operations as its the core part, any
 * failure here the governor may behave unpredictably so redundancies have 
 * been added to prevent that.
 */
int start_gov_setup(struct cpufreq_policy *policy)
{
	int err = 0;
	
	PDEBUG("Starting Governor for cpu:%d",policy->cpu);
	
	/* aquire lock so that we dont overwrite critical sections which could lead to wrong 
	 * data being taken in and improper working of governor
	 */
	err = get_cpufreq_table(policy);
	if(err)
		goto error;

	/* setup kthread for endurance governing skip is it has already been setup */
	if(governor_enabled == 1)
	{
		atomic_notifier_chain_register(&therm_alert_notifier_head,
								&therm_notifier_block);
		atomic_notifier_chain_register(&load_change_notifier_head,
								&load_chg_notifier_block);
		PDEBUG("Run kthread");
		wake_up_process(therm_mon_task);
	}

	PDEBUG("Finished setup");
	
	return 0;
error:
	pr_err(KERN_INFO"%s: Failed to setup governor.\n", __func__);
	return 0;
}

/* 
 *cfe_cleanup() dealloctates all structures before exiting for each cluster
 */
static void cfe_cleanup(void)
{
	struct cluster_prop *cluster = NULL;
	unsigned int cpu;
	
	for_each_possible_cpu(cpu){
		cluster = per_cpu(cluster_nr,cpu);
		if(cluster){
			kfree(cluster->cached_tunables);
			kfree(cluster);
			}
	}
	kfree(thermal_monitor);
}

static int cpufreq_governor_endurance(struct cpufreq_policy *policy,
					unsigned int event)
{
	struct cluster_prop *cluster;

	switch (event) {
	case CPUFREQ_GOV_START:
		mutex_lock(&gov_lock);
		start_gov_setup(policy);
		mutex_unlock(&gov_lock);
		break;
	case CPUFREQ_GOV_LIMITS:
		cfe_reset_params(policy);
		govern_cpu(per_cpu(cluster_nr,policy->cpu));
		break;
	case CPUFREQ_GOV_STOP:
		mutex_lock(&gov_lock);
		governor_enabled--;
		cluster = per_cpu(cluster_nr, policy->cpu);
		cluster->gov_enabled = 0;
		if(!governor_enabled){
			atomic_notifier_chain_unregister(
					&therm_alert_notifier_head,
					&therm_notifier_block);
			atomic_notifier_chain_unregister(
					&load_change_notifier_head,
					&load_chg_notifier_block);
		}
		sysfs_remove_group(get_governor_parent_kobj(policy),get_sysfs_attr());
		policy->governor_data = NULL;
		mutex_unlock(&gov_lock);
		break;
	default:
		break;
	}
	return 0;
}

#ifdef CONFIG_CPU_FREQ_GOV_ENDURANCE_MODULE
static
#endif
struct cpufreq_governor cpufreq_gov_endurance = {
	.name		= "endurance",
	.governor	= cpufreq_governor_endurance,
	.owner		= THIS_MODULE,
};

static int __init cpufreq_gov_endurance_init(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-2 };
	
	mutex_init(&gov_lock);
	mutex_init(&rw_lock);

	therm_mon_task =
			kthread_create(cfe_thermal_monitor_task, NULL,
				       "cfendurance");
	if (IS_ERR(therm_mon_task))
		return PTR_ERR(therm_mon_task);

	sched_setscheduler_nocheck(therm_mon_task, SCHED_FIFO, &param);
	get_task_struct(therm_mon_task);

	/* NB: wake up so the thread does not look hung to the freezer */
	wake_up_process(therm_mon_task);

	return cpufreq_register_governor(&cpufreq_gov_endurance);
}

static void __exit cpufreq_gov_endurance_exit(void)
{
	kthread_stop(therm_mon_task);
	put_task_struct(therm_mon_task);
	cfe_cleanup();
	cpufreq_unregister_governor(&cpufreq_gov_endurance);
}

MODULE_AUTHOR("Tejas Udupa <tejasudupa1285@gmail>");
MODULE_DESCRIPTION("'cpufreq_endurance' - A governor that is based"
	"on 'performance' by Dominik Brodowski,"
	"pushes cpufreq to max avialable frequency now modified to"
	"push max frequency depending on temprature on the given sensor");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ENDURANCE
fs_initcall(cpufreq_gov_endurance_init);
#else
module_init(cpufreq_gov_endurance_init);
#endif
module_exit(cpufreq_gov_endurance_exit);
