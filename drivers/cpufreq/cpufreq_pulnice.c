/*
 * CPU Frequency Governor: Pulnice (Safe Threshold Governor)
 * - Basic threshold-based scaling
 * - SysFS tunables with proper locking
 * - Built-in utilization tracking
 */

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>  // Fix for ktime_get_us
#include <linux/time.h>

struct pulnice_policy {
    struct cpufreq_policy *policy;
    spinlock_t lock;
    
    // Tunables
    unsigned int util_high;
    unsigned int util_low;
    unsigned int rate_limit_us;
    u64 last_update;      // Track last update time
};


/* Default tunables */
#define DEFAULT_UTIL_HIGH      70
#define DEFAULT_UTIL_LOW       40
#define DEFAULT_RATE_LIMIT_US  50000  // 50ms

/*********************
 * SysFS Interface
 *********************/
static ssize_t show_util_high(struct cpufreq_policy *policy, char *buf)
{
    struct pulnice_policy *pn = policy->governor_data;
    return sprintf(buf, "%u\n", pn->util_high);
}

static ssize_t store_util_high(struct cpufreq_policy *policy, const char *buf,
                             size_t count)
{
    struct pulnice_policy *pn = policy->governor_data;
    unsigned int value;
    
    if (kstrtouint(buf, 0, &value) || value > 100)
        return -EINVAL;
    
    spin_lock(&pn->lock);
    pn->util_high = value;
    spin_unlock(&pn->lock);
    return count;
}

static ssize_t show_util_low(struct cpufreq_policy *policy, char *buf)
{
    struct pulnice_policy *pn = policy->governor_data;
    return sprintf(buf, "%u\n", pn->util_low);
}

static ssize_t store_util_low(struct cpufreq_policy *policy, const char *buf,
                            size_t count)
{
    struct pulnice_policy *pn = policy->governor_data;
    unsigned int value;
    
    if (kstrtouint(buf, 0, &value) || value > 100)
        return -EINVAL;
    
    spin_lock(&pn->lock);
    pn->util_low = value;
    spin_unlock(&pn->lock);
    return count;
}

static ssize_t show_rate_limit(struct cpufreq_policy *policy, char *buf)
{
    struct pulnice_policy *pn = policy->governor_data;
    return sprintf(buf, "%u\n", pn->rate_limit_us);
}

static ssize_t store_rate_limit(struct cpufreq_policy *policy, const char *buf,
                              size_t count)
{
    struct pulnice_policy *pn = policy->governor_data;
    unsigned int value;
    
    if (kstrtouint(buf, 0, &value))
        return -EINVAL;
    
    spin_lock(&pn->lock);
    pn->rate_limit_us = value;
    spin_unlock(&pn->lock);
    return count;
}

/* Attribute declarations */
static struct freq_attr util_high = __ATTR(util_high, 0644, show_util_high, store_util_high);
static struct freq_attr util_low = __ATTR(util_low, 0644, show_util_low, store_util_low);
static struct freq_attr rate_limit = __ATTR(rate_limit_us, 0644, show_rate_limit, store_rate_limit);

static struct attribute *pulnice_attrs[] = {
    &util_high.attr,
    &util_low.attr,
    &rate_limit.attr,
    NULL
};

static struct attribute_group pulnice_attr_group = {
    .attrs = pulnice_attrs,
    .name = "pulnice",
};

/*********************
 * Core Logic
 *********************/
static unsigned int get_util(struct cpufreq_policy *policy)
{
    return (policy->cpuinfo.max_freq * arch_scale_cpu_capacity(NULL, policy->cpu)) 
            >> SCHED_CAPACITY_SHIFT;
}

static void update_policy(struct pulnice_policy *pn)
{
    struct cpufreq_policy *policy;
    unsigned int util, new_freq;
    unsigned long flags;
    u64 now;

    spin_lock_irqsave(&pn->lock, flags);
    
    policy = pn->policy;
    now = ktime_get_ns() / NSEC_PER_USEC;
    
    /* Rate limiting */
    if (now < pn->last_update + pn->rate_limit_us)
        goto out_unlock;
    
    util = get_util(policy);
    
    if (util >= pn->util_high) {
        new_freq = policy->max;
    } else if (util <= pn->util_low) {
        new_freq = policy->min;
    } else {
        goto out_unlock;
    }
    
    if (new_freq != policy->cur) {
        pn->last_update = now;
        spin_unlock_irqrestore(&pn->lock, flags);
        
        __cpufreq_driver_target(policy, new_freq, CPUFREQ_RELATION_C);
        return;
    }

out_unlock:
    spin_unlock_irqrestore(&pn->lock, flags);
}

/*********************
 * Governor Hooks
 *********************/
static int pulnice_init(struct cpufreq_policy *policy)
{
    struct pulnice_policy *pn;
    
    pn = kzalloc(sizeof(*pn), GFP_KERNEL);
    if (!pn)
        return -ENOMEM;
    
    pn->policy = policy;
    spin_lock_init(&pn->lock);
    
    pn->util_high = DEFAULT_UTIL_HIGH;
    pn->util_low = DEFAULT_UTIL_LOW;
    pn->rate_limit_us = DEFAULT_RATE_LIMIT_US;
    pn->last_update = 0;
    
    policy->governor_data = pn;
    
    if (cpufreq_global_kobject) {
        sysfs_create_group(&policy->kobj, &pulnice_attr_group);
    }
    
    return 0;
}

static void pulnice_exit(struct cpufreq_policy *policy)
{
    struct pulnice_policy *pn = policy->governor_data;
    
    sysfs_remove_group(&policy->kobj, &pulnice_attr_group);
    kfree(pn);
    policy->governor_data = NULL;
}

static int pulnice_start(struct cpufreq_policy *policy)
{
    return 0;
}

static void pulnice_stop(struct cpufreq_policy *policy)
{
}

static void pulnice_limits(struct cpufreq_policy *policy)
{
    struct pulnice_policy *pn = policy->governor_data;
    unsigned long flags;

    if (!policy->fast_switch_enabled) {
        spin_lock_irqsave(&policy->transition_lock, flags);
        cpufreq_verify_within_limits(policy, 
            policy->cpuinfo.min_freq, 
            policy->cpuinfo.max_freq);
        spin_unlock_irqrestore(&policy->transition_lock, flags);
    }

    update_policy(pn);
}

static struct cpufreq_governor cpufreq_gov_pulnice = {
    .name       = "pulnice",
    .init       = pulnice_init,
    .exit       = pulnice_exit,
    .start      = pulnice_start,
    .stop       = pulnice_stop,
    .limits     = pulnice_limits,
    .owner      = THIS_MODULE,
};

static int __init cpufreq_pulnice_init(void)
{
    return cpufreq_register_governor(&cpufreq_gov_pulnice);
}

static void __exit cpufreq_pulnice_exit(void)
{
    cpufreq_unregister_governor(&cpufreq_gov_pulnice);
}

module_init(cpufreq_pulnice_init);
module_exit(cpufreq_pulnice_exit);

MODULE_AUTHOR("Boyan Spassov");
MODULE_DESCRIPTION("Stable Threshold CPU Frequency Governor");
MODULE_LICENSE("GPL");
