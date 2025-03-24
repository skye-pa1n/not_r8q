/*
 * CPU Frequency Governor: Pulnice (Optimized Thresholds)
 * - Faster frequency drop when utilization decreases
 * - Tighter hysteresis window
 * - Frequency stabilization period
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/jiffies.h>

#define UTIL_HIGH          69  // Boost threshold
#define UTIL_LOW           60  // Drop threshold (reduced from 50)
#define SAMPLING_INTERVAL  100 // ms (faster sampling)
#define RATE_LIMIT         50  // ms (faster response)
#define MIN_FREQ_DURATION  300 // ms (stay at min freq for at least 300ms)

struct pulnice_data {
    unsigned int target_freq;
    unsigned long last_updated;
    unsigned long min_freq_until; // Timestamp until we stay at min freq
};

static DEFINE_MUTEX(pulnice_lock);

static unsigned int get_safe_util(unsigned int cpu)
{
    u64 wall, idle, delta_wall, delta_idle;
    static u64 last_wall, last_idle;
    unsigned int util;
    
    wall = get_cpu_idle_time(cpu, &idle, 1);

    // Handle counter reset/wraparound
    if (wall < last_wall || idle < last_idle) {
        last_wall = wall;
        last_idle = idle;
        return 0;
    }

    delta_wall = wall - last_wall;
    delta_idle = idle - last_idle;

    // Minimum 20ms measurement window for stable readings
    if (delta_wall < 20000000) // 20ms in ns
        return 0;

    util = 100 * (delta_wall - delta_idle) / delta_wall;
    
    last_wall = wall;
    last_idle = idle;
    
    return clamp_val(util, 0, 100);
}

static void update_pulnice(struct cpufreq_policy *policy)
{
    struct pulnice_data *data = policy->governor_data;
    unsigned int util, new_freq;
    unsigned long now = jiffies;

    mutex_lock(&pulnice_lock);

    // Enforce minimum frequency duration
    if (time_before(now, data->min_freq_until)) {
        mutex_unlock(&pulnice_lock);
        return;
    }

    // Rate limiting check
    if (time_before(now, data->last_updated + 
                   msecs_to_jiffies(RATE_LIMIT))) {
        mutex_unlock(&pulnice_lock);
        return;
    }

    util = get_safe_util(policy->cpu);

    if (util >= UTIL_HIGH) {
        new_freq = policy->max;
        data->min_freq_until = 0; // Reset min freq lock
    } else if (util <= UTIL_LOW || data->target_freq == policy->min) {
        // Aggressive drop to min freq
        new_freq = policy->min;
        // Stay at min freq for at least MIN_FREQ_DURATION
        data->min_freq_until = now + msecs_to_jiffies(MIN_FREQ_DURATION);
    } else {
        mutex_unlock(&pulnice_lock);
        return;
    }

    if (new_freq != policy->cur) {
        data->target_freq = new_freq;
        __cpufreq_driver_target(policy, new_freq, CPUFREQ_RELATION_C);
        data->last_updated = now;
        pr_debug("Pulnice: CPU%u %u→%uMHz (%u%%) %s",
               policy->cpu, policy->cur/1000, new_freq/1000, util,
               new_freq == policy->min ? "(MIN-LOCK)" : "");
    }

    mutex_unlock(&pulnice_lock);
}

/* Governor callbacks */
static int pulnice_init(struct cpufreq_policy *policy)
{
    struct pulnice_data *data;

    data = kzalloc(sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->target_freq = policy->cur;
    policy->governor_data = data;
    return 0;
}

static void pulnice_exit(struct cpufreq_policy *policy)
{
    kfree(policy->governor_data);
    policy->governor_data = NULL;
}

static int pulnice_start(struct cpufreq_policy *policy)
{
    pr_info("Pulnice active for CPU%u\n", policy->cpu);
    update_pulnice(policy);
    return 0;
}

static void pulnice_stop(struct cpufreq_policy *policy)
{
    pr_info("Pulnice stopped for CPU%u\n", policy->cpu);
}

static void pulnice_limits(struct cpufreq_policy *policy)
{
    update_pulnice(policy);
}

static struct cpufreq_governor cpufreq_gov_pulnice = {
    .name        = "pulnice",
    .init        = pulnice_init,
    .exit        = pulnice_exit,
    .start       = pulnice_start,
    .stop        = pulnice_stop,
    .limits      = pulnice_limits,
    .owner       = THIS_MODULE,
};

static int __init pulnice_register(void)
{
    return cpufreq_register_governor(&cpufreq_gov_pulnice);
}

static void __exit pulnice_unregister(void)
{
    cpufreq_unregister_governor(&cpufreq_gov_pulnice);
}

module_init(pulnice_register);
module_exit(pulnice_unregister);

MODULE_AUTHOR("Boyan Spassov");
MODULE_DESCRIPTION("Stable Threshold CPU Frequency Governor");
MODULE_LICENSE("GPL");