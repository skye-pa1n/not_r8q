#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define DEFAULT_UP_THRESHOLD 77
#define DEFAULT_DOWN_THRESHOLD 23
#define DEFAULT_SAMPLING_RATE 20000 // in microseconds

struct uag_governor {
    unsigned int up_threshold;
    unsigned int down_threshold;
    unsigned int sampling_rate;
};

static struct uag_governor uag_params = {
    .up_threshold = DEFAULT_UP_THRESHOLD,
    .down_threshold = DEFAULT_DOWN_THRESHOLD,
    .sampling_rate = DEFAULT_SAMPLING_RATE,
};

static int uag_should_increase_freq(unsigned int load) {
    return load > uag_params.up_threshold;
}

static int uag_should_decrease_freq(unsigned int load) {
    return load < uag_params.down_threshold;
}

static void uag_update_freq(struct cpufreq_policy *policy, unsigned int load) {
    if (uag_should_increase_freq(load)) {
        cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);
    } else if (uag_should_decrease_freq(load)) {
        cpufreq_driver_target(policy, policy->min, CPUFREQ_RELATION_L);
    }
}

static int uag_governor_thread(void *data) {
    struct cpufreq_policy *policy = data;

    while (!kthread_should_stop()) {
        unsigned int load = 0;

        load = get_cpu_idle_time_us(policy->cpu);

        uag_update_freq(policy, load);

        // Sleep for the sampling rate
        usleep_range(uag_params.sampling_rate, uag_params.sampling_rate + 1000);
    }

    return 0;
}

static struct cpufreq_governor cpufreq_gov_uag = {
    .name = "uag",
    .governor = uag_governor_thread,
    .owner = THIS_MODULE,
};

static int __init uag_init(void) {
    pr_info("UAG Governor: Initializing\n");
    return cpufreq_register_governor(&cpufreq_gov_uag);
}

static void __exit uag_exit(void) {
    pr_info("UAG Governor: Exiting\n");
    cpufreq_unregister_governor(&cpufreq_gov_uag);
}

module_init(uag_init);
module_exit(uag_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bombed skye");
MODULE_DESCRIPTION("Ultimate Aggressive Governor");

