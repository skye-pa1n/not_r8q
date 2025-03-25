/*
 * CPU Frequency Governor: Pulnice (Safe Threshold Governor)
 * - Uses 3rd lowest frequency in passive state
 * - SysFS tunables with mutex protection
 * - Robust frequency table handling
 */

#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/sort.h>

struct pulnice_policy {
    struct cpufreq_policy *policy;
    struct mutex lock;
    
    // Tunables
    unsigned int util_high;
    unsigned int util_low;
    unsigned int rate_limit_us;
    u64 last_update;
    
    // Frequency management
    unsigned int passive_freq;  // 3rd lowest freq
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
    
    mutex_lock(&pn->lock);
    pn->util_high = value;
    mutex_unlock(&pn->lock);
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
    
    mutex_lock(&pn->lock);
    pn->util_low = value;
    mutex_unlock(&pn->lock);
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
    
    mutex_lock(&pn->lock);
    pn->rate_limit_us = value;
    mutex_unlock(&pn->lock);
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
 * Frequency Helpers
 *********************/
static int compare_freq(const void *a, const void *b)
{
    return *(unsigned int *)a - *(unsigned int *)b;
}

static void init_passive_freq(struct pulnice_policy *pn)
{
    struct cpufreq_policy *policy = pn->policy;
    unsigned int *freqs = NULL;
    int count = 0, i, j;

    // Collect unique valid frequencies
    for (i = 0; policy->freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
        unsigned int freq = policy->freq_table[i].frequency;
        
        if (freq == CPUFREQ_ENTRY_INVALID)
            continue;
            
        // Check for duplicates
        for (j = 0; j < count; j++) {
            if (freqs[j] == freq)
                break;
        }
        
        if (j == count) { // New unique freq
            unsigned int *new_freqs = krealloc(freqs, (count + 1) * sizeof(*freqs),
                                             GFP_KERNEL);
            if (!new_freqs) {
                kfree(freqs);
                return;
            }
            freqs = new_freqs;
            freqs[count++] = freq;
        }
    }

    // Sort and select 3rd lowest
    if (count > 0) {
        sort(freqs, count, sizeof(*freqs), compare_freq, NULL);
        pn->passive_freq = (count >= 3) ? freqs[2] : freqs[count - 1];
    } else {
        pn->passive_freq = policy->min;
    }
    
    kfree(freqs);
}

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
    u64 now;

    mutex_lock(&pn->lock);
    
    policy = pn->policy;
    now = ktime_to_us(ktime_get());
    
    /* Rate limiting */
    if (now < pn->last_update + pn->rate_limit_us) {
        mutex_unlock(&pn->lock);
        return;
    }
    
    util = get_util(policy);
    
    if (util >= pn->util_high) {
        new_freq = policy->max;
    } else if (util <= pn->util_low) {
        new_freq = policy->min;
    } else {  // Passive state
        new_freq = pn->passive_freq;
    }
    
    if (new_freq != policy->cur) {
        pn->last_update = now;
        __cpufreq_driver_target(policy, new_freq, CPUFREQ_RELATION_C);
    }
    
    mutex_unlock(&pn->lock);
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
    mutex_init(&pn->lock);
    
    // Initialize frequencies
    init_passive_freq(pn);
    
    // Set defaults
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
