#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/ktime.h>

static struct timer_list governor_timer;
static unsigned long last_cpu_load[NR_CPUS] = {0};  // Store last CPU load for each CPU
static unsigned long last_check_time[NR_CPUS] = {0}; // Store last check time for each CPU

// Simplified load calculation using the cpufreq_stats
static unsigned long get_cpu_load(int cpu)
{
    unsigned long load;
    struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

    if (!policy)
        return 0;

    // Use the CPU frequency statistics to estimate load
    load = cpufreq_get(policy->cpu) * 100 / policy->max;  // Simple approximation

    cpufreq_cpu_put(policy);
    return load;
}

// Timer function to periodically calculate the CPU load and adjust frequency
static void governor_timer_func(struct timer_list *t)
{
    int cpu;
    unsigned long load;
    unsigned int target_freq;
    struct cpufreq_policy *policy;

    for_each_online_cpu(cpu) {
        policy = cpufreq_cpu_get(cpu);
        if (!policy)
            continue;

        // Get CPU load if enough time has passed since the last check
        if (ktime_to_ms(ktime_get()) - last_check_time[cpu] > 500) {  // Check every 500ms
            load = get_cpu_load(cpu);
            last_check_time[cpu] = ktime_to_ms(ktime_get());  // Update last check time

            // Set target frequency based on load
            if (load > 80) {
                target_freq = policy->max;
            } else if (load > 60) {
                target_freq = (policy->max * 67) / 100;
            } else if (load > 40) {
                target_freq = (policy->max * 40) / 100;
            } else {
                target_freq = (policy->max * 20) / 100;
            }

            // Set the CPU frequency using cpufreq_driver_target
            if (cpufreq_driver_target(policy, target_freq, CPUFREQ_RELATION_H)) {
                pr_err("Failed to set frequency %u kHz for CPU %d\n", target_freq, cpu);
            } else {
                pr_info("CPU%d: Load: %lu%%, Frequency set to %u kHz\n", cpu, load, target_freq);
            }
        }

        cpufreq_cpu_put(policy);
    }

    // Reschedule the timer to run again
    mod_timer(&governor_timer, jiffies + msecs_to_jiffies(500));  // Run every 500ms
}

// Governor initialization function
static int not_uag_init(struct cpufreq_policy *policy)
{
    pr_info("not_uag governor started\n");

    // Initialize the timer
    timer_setup(&governor_timer, governor_timer_func, 0);
    mod_timer(&governor_timer, jiffies + msecs_to_jiffies(500));  // Start the timer with 500ms delay

    return 0;
}

static int not_uag_start(struct cpufreq_policy *policy)
{
    pr_info("not_uag governor started\n");
    return 0;
}

static void not_uag_stop(struct cpufreq_policy *policy)
{
    pr_info("not_uag governor stopped\n");

    // Remove the timer when stopping
    del_timer_sync(&governor_timer);
}

static struct cpufreq_governor not_uag_governor = {
    .name = "not_uag",
    .start = not_uag_start,
    .stop = not_uag_stop,
    .init = not_uag_init,
};

static int __init not_uag_init_module(void)
{
    int ret;

    ret = cpufreq_register_governor(&not_uag_governor);
    if (ret)
        pr_err("Failed to register not_uag governor\n");
    else
        pr_info("not_uag governor registered\n");

    return ret;
}

static void __exit not_uag_exit_module(void)
{
    cpufreq_unregister_governor(&not_uag_governor);
    pr_info("not_uag governor unregistered\n");
}

module_init(not_uag_init_module);
module_exit(not_uag_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("skyewasnthere");
MODULE_DESCRIPTION("Optimized Ultra Aggressive CPU governor for Android Kernel");
