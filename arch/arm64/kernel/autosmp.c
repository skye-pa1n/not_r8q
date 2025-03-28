/*
 * arch/arm64/kernel/autosmp.c
 *
 * Automatically hotplug/unplug multiple cpu cores
 * based on cpu load and suspend state
 *
 * Based on the msm_mpdecision code by
 * Copyright (c) 2012-2013, Dennis Rassmann <showp1984@gmail.com>
 *
 * Copyright (C) 2013-2014, Rauf Gungor, http://github.com/mrg666
 * rewrite to simplify and optimize, Jul. 2013, http://goo.gl/cdGw6x
 * optimize more, generalize for n cores, Sep. 2013, http://goo.gl/448qBz
 * generalize for all arch, rename as autosmp, Dec. 2013, http://goo.gl/x5oyhy
 *
 * Copyright (C) 2018, Ryan Andri (Rainforce279) <ryanandri@linuxmail.org>
 *       Adaptation for Octa core processor.
 *
 * Copyright (C) 2025, João Batista (skye-pa1n) <sonicgames772@gmail.com>
 *       Fixup cpu cluster logic & add prime cluster logic in newer octa core processors.
 *       Based on Ryan Andri work "Adaptation for Octa core processor.".
 *       Introduce a more precise cpu load calculation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. For more details, see the GNU
 * General Public License included with the Linux kernel or available
 * at www.gnu.org/licenses
 */

#include <linux/moduleparam.h>
#include <linux/cpufreq.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/fb.h>

#define ASMP_TAG "AutoSMP: "

struct asmp_load_data {
    u64 prev_cpu_idle;
    u64 prev_cpu_wall;
};
static DEFINE_PER_CPU(struct asmp_load_data, asmp_data);

static struct delayed_work asmp_work;
static struct workqueue_struct *asmp_workq;
static struct notifier_block asmp_nb;

/*
 * Flag and NOT editable/tunabled
 */
static bool started = false;

static struct asmp_param_struct {
    unsigned int delay;
    bool scroff_single_core;
    unsigned int max_cpus_pr;
    unsigned int max_cpus_bc;
    unsigned int max_cpus_lc;
    unsigned int min_cpus_pr;
    unsigned int min_cpus_bc;
    unsigned int min_cpus_lc;
    unsigned int cpufreq_up_pr;
    unsigned int cpufreq_up_bc;
    unsigned int cpufreq_up_lc;
    unsigned int cpufreq_down_pr;
    unsigned int cpufreq_down_bc;
    unsigned int cpufreq_down_lc;
    unsigned int cycle_up;
    unsigned int cycle_down;
} asmp_param = {
    .delay = 72,
    .scroff_single_core = true,
    .max_cpus_pr = 1, /* Max cpu Prime cluster ! */ 
    .max_cpus_bc = 3, /* Max cpu Big cluster ! */
    .max_cpus_lc = 4, /* Max cpu Little cluster ! */
    .min_cpus_pr = 0, /* Minimum Prime cluster online */
    .min_cpus_bc = 0, /* Minimum Big cluster online */
    .min_cpus_lc = 1, /* Minimum Little cluster online */
    .cpufreq_up_pr = 40,
    .cpufreq_up_bc = 45,
    .cpufreq_up_lc = 35,
    .cpufreq_down_pr = 30,
    .cpufreq_down_bc = 30,
    .cpufreq_down_lc = 25,
    .cycle_up = 0,
    .cycle_down = 3,
};

static unsigned int cycle = 0, delay0 = 0;
static unsigned long delay_jif = 0;
int asmp_enabled __read_mostly = 0;

static void asmp_ctrl_cpu(unsigned int cpu, bool online)
{
    struct device *dev;

    lock_device_hotplug();
    dev = get_cpu_device(cpu);
    if (dev) {
        if (online)
            device_online(dev);
        else
            device_offline(dev);
    }
    unlock_device_hotplug();
}

static void asmp_online_cpus(unsigned int cpu)
{
    asmp_ctrl_cpu(cpu, true);
}

static void asmp_offline_cpus(unsigned int cpu)
{
    asmp_ctrl_cpu(cpu, false);
}


static int get_cpu_loads(unsigned int cpu)
{
    struct asmp_load_data *data = &per_cpu(asmp_data, cpu);
    u64 cur_wall_time, cur_idle_time;
    s64 wall_time, idle_time;
    unsigned int load = 0;

    cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, 0);
    wall_time = cur_wall_time - data->prev_cpu_wall;
    idle_time = cur_idle_time - data->prev_cpu_idle;
    data->prev_cpu_wall = cur_wall_time;
    data->prev_cpu_idle = cur_idle_time;

    if (wall_time <= 0 || idle_time < 0 || wall_time < idle_time) {
        return 0;
    }

    /* Try to avoid precision loss */
    load = (100 * (wall_time - idle_time) + (wall_time / 2)) / wall_time;

    /* Clamp load to [0, 100] range for obvious reasons */
    if (load < 0) {
        load = 0;
    } else if (load > 100) {
        load = 100;
    }

    return load;
}


static void update_prev_idle(unsigned int cpu)
{
    /* Record cpu idle data for next calculation loads */
    struct asmp_load_data *data = &per_cpu(asmp_data, cpu);
    data->prev_cpu_idle = get_cpu_idle_time(cpu,
                &data->prev_cpu_wall, 0);
}

static void __ref asmp_work_fn(struct work_struct *work) {
    unsigned int cpu = 0, load = 0;
    unsigned int slow_cpu_pr = 7, slow_cpu_bc = 4, slow_cpu_lc = 0;
    unsigned int cpu_load_pr = 0, fast_load_pr = 0;
    unsigned int cpu_load_bc = 0, fast_load_bc = 0;
    unsigned int cpu_load_lc = 0, fast_load_lc = 0;
    unsigned int slow_load_lc = 100, slow_load_bc = 100, slow_load_pr = 100;
    unsigned int up_load_lc = 0, down_load_lc = 0;
    unsigned int up_load_bc = 0, down_load_bc = 0;
    unsigned int up_load_pr = 0, down_load_pr = 0;
    unsigned int max_cpu_lc = 0, max_cpu_bc = 0, max_cpu_pr = 0;
    unsigned int min_cpu_lc = 0, min_cpu_bc = 0, min_cpu_pr = 0;
    int nr_cpu_online_lc = 0, nr_cpu_online_bc = 0, nr_cpu_online_pr = 0;

    /* Perform always check cpu 0/4/7 */
    if (!cpu_online(7))
        asmp_online_cpus(7);
    if (!cpu_online(4))
        asmp_online_cpus(4);
    if (!cpu_online(0))
        asmp_online_cpus(0);

    cycle++;

    if (asmp_param.delay != delay0) {
        delay0 = asmp_param.delay;
        delay_jif = msecs_to_jiffies(delay0);
    }

    /* Little Cluster */
    up_load_lc   = asmp_param.cpufreq_up_lc;
    down_load_lc = asmp_param.cpufreq_down_lc;
    max_cpu_lc = asmp_param.max_cpus_lc;
    min_cpu_lc = asmp_param.min_cpus_lc;

    /* Big Cluster */
    up_load_bc   = asmp_param.cpufreq_up_bc;
    down_load_bc = asmp_param.cpufreq_down_bc;
    max_cpu_bc = asmp_param.max_cpus_bc;
    min_cpu_bc = asmp_param.min_cpus_bc;
    
    /* Prime Cluster */
    up_load_pr   = asmp_param.cpufreq_up_pr;
    down_load_pr = asmp_param.cpufreq_down_pr;
    max_cpu_pr = asmp_param.max_cpus_pr;
    min_cpu_pr = asmp_param.min_cpus_pr;

    /* find current max and min cpu freq to estimate load */
    cpus_read_lock();
    cpu_load_lc = get_cpu_loads(0);
    fast_load_lc = cpu_load_lc;
    cpu_load_bc = get_cpu_loads(4);
    fast_load_bc = cpu_load_bc;
    cpu_load_pr = get_cpu_loads(7);
    fast_load_pr = cpu_load_pr;
    for_each_online_cpu(cpu) {
       if (cpu == 7) {
            nr_cpu_online_pr++;
            load = get_cpu_loads(cpu);
            if (load < slow_load_pr) {
                slow_cpu_pr = cpu;
                slow_load_pr = load;
            } else if (load > fast_load_pr)
                fast_load_pr = load;
        }
        
        if (cpu < 7 && cpu > 4) {
            nr_cpu_online_bc++;
            load = get_cpu_loads(cpu);
            if (load < slow_load_bc) {
                slow_cpu_bc = cpu;
                slow_load_bc = load;
            } else if (load > fast_load_bc)
                fast_load_bc = load;
        }

        if (cpu && cpu < 4) {
            nr_cpu_online_lc++;
            load = get_cpu_loads(cpu);
            if (load < slow_load_lc) {
                slow_cpu_lc = cpu;
                slow_load_lc = load;
            } else if (load > fast_load_lc)
                fast_load_lc = load;
        }
    }
    cpus_read_unlock();
    
    /********************************************************************
     *                     Prime Cluster cpu(7)                     *
     ********************************************************************/
    if (cpu_load_pr < slow_load_pr)
        slow_load_pr = cpu_load_pr;

    /* Always check cpu 4 before + up nr */
    if (cpu_online(7))
        nr_cpu_online_pr += 1;

    /* hotplug one core if all online cores are over up_load limit */
    if (slow_load_pr > up_load_pr) {
        if ((nr_cpu_online_pr < max_cpu_pr) &&
            (cycle >= asmp_param.cycle_up)) {
            cpu = cpumask_next_zero(7, cpu_online_mask);
            asmp_online_cpus(cpu);
            cycle = 0;
        }
    /* unplug slowest core if all online cores are under down_load limit */
    } else if ((slow_cpu_pr > 6) && (fast_load_pr < down_load_pr)) {
        if ((nr_cpu_online_pr > min_cpu_pr) &&
            (cycle >= asmp_param.cycle_down)) {
            asmp_offline_cpus(slow_cpu_pr);
            cycle = 0;
        }
    }

    /********************************************************************
     *                     Big Cluster cpu(4..6)                     *
     ********************************************************************/
    if (cpu_load_bc < slow_load_bc)
        slow_load_bc = cpu_load_bc;

    /* Always check cpu 4 before + up nr */
    if (cpu_online(4))
        nr_cpu_online_bc += 1;

    /* hotplug one core if all online cores are over up_load limit */
    if (slow_load_bc > up_load_bc) {
        if ((nr_cpu_online_bc < max_cpu_bc) &&
            (cycle >= asmp_param.cycle_up)) {
            cpu = cpumask_next_zero(4, cpu_online_mask);
            asmp_online_cpus(cpu);
            cycle = 0;
        }
    /* unplug slowest core if all online cores are under down_load limit */
    } else if ((slow_cpu_bc > 4) && (slow_cpu_bc < 7) && (fast_load_bc < down_load_bc)) {
        if ((nr_cpu_online_bc > min_cpu_bc) &&
            (cycle >= asmp_param.cycle_down)) {
            asmp_offline_cpus(slow_cpu_bc);
            cycle = 0;
        }
    }

    /********************************************************************
     *                      Little Cluster cpu(0..3)                       *
     ********************************************************************/
    if (cpu_load_lc < slow_load_lc)
        slow_load_lc = cpu_load_lc;

    /* Always check cpu 0 before + up nr */
    if (cpu_online(0))
        nr_cpu_online_lc += 1;

    /* hotplug one core if all online cores are over up_load limit */
    if (slow_load_lc > up_load_lc) {
        if ((nr_cpu_online_lc < max_cpu_lc) &&
            (cycle >= asmp_param.cycle_up)) {
            cpu = cpumask_next_zero(0, cpu_online_mask);
            asmp_online_cpus(cpu);
            cycle = 0;
        }
    /* unplug slowest core if all online cores are under down_load limit */
    } else if (slow_cpu_lc && (fast_load_lc < down_load_lc)) {
        if ((nr_cpu_online_lc > min_cpu_lc) &&
            (cycle >= asmp_param.cycle_down)) {
            asmp_offline_cpus(slow_cpu_lc);
            cycle = 0;
        }
    }

    /*
     * Reflect to any users configure about min cpus.
     * give a delay for atleast 2 seconds to prevent
     * wrong cpu loads calculation.
     */
    if (nr_cpu_online_lc < min_cpu_lc || nr_cpu_online_lc < min_cpu_lc) {
        for_each_possible_cpu(cpu) {
            /* Online All cores */
            if (!cpu_online(cpu))
                asmp_online_cpus(cpu);

            update_prev_idle(cpu);
        }
        delay_jif = msecs_to_jiffies(2000);
    }

    queue_delayed_work(asmp_workq, &asmp_work, delay_jif);
}

static void __ref asmp_suspend(void)
{
    unsigned int cpu = 0;

    /* stop plug/unplug when suspend */
    cancel_delayed_work_sync(&asmp_work);

    /* leave only cpu 0 and cpu 4 to stay online */
    for_each_online_cpu(cpu) {
        if (cpu && cpu != 4)
            asmp_offline_cpus(cpu);
    }
}

static void __ref asmp_resume(void)
{
    unsigned int cpu = 0;

    /* Force all cpu's to online when resumed */
    for_each_possible_cpu(cpu) {
        if (!cpu_online(cpu))
            asmp_online_cpus(cpu);

        update_prev_idle(cpu);
    }

    /* rescheduled queue atleast on 3 seconds */
    queue_delayed_work(asmp_workq, &asmp_work,
                msecs_to_jiffies(3000));
}

static int asmp_notifier_cb(struct notifier_block *nb,
                unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;

    if (evdata && evdata->data &&
        event == FB_EVENT_BLANK) {
        blank = evdata->data;
        if (*blank == FB_BLANK_UNBLANK) {
            if (asmp_param.scroff_single_core)
                asmp_resume();
        } else if (*blank == FB_BLANK_POWERDOWN) {
            if (asmp_param.scroff_single_core)
                asmp_suspend();
        }
    }

    return 0;
}

static int __ref asmp_start(void)
{
    unsigned int cpu = 0;
    int ret = 0;

    if (started) {
        pr_info(ASMP_TAG"already enabled\n");
        return ret;
    }

    asmp_workq = alloc_workqueue("asmp", WQ_HIGHPRI, 0);
    if (!asmp_workq) {
        ret = -ENOMEM;
        goto err_out;
    }

    for_each_possible_cpu(cpu) {
        /* Online All cores */
        if (!cpu_online(cpu))
            asmp_online_cpus(cpu);

        update_prev_idle(cpu);
    }

    INIT_DELAYED_WORK(&asmp_work, asmp_work_fn);
    queue_delayed_work(asmp_workq, &asmp_work,
            msecs_to_jiffies(asmp_param.delay));

    asmp_nb.notifier_call = asmp_notifier_cb;
    if (fb_register_client(&asmp_nb))
        pr_info("%s: failed register to fb notifier\n", __func__);

    started = true;

    pr_info(ASMP_TAG"enabled\n");

    return ret;

err_out:

    asmp_enabled = 0;
    return ret;
}

static void __ref asmp_stop(void)
{
    unsigned int cpu = 0;

    if (!started) {
        pr_info(ASMP_TAG"already disabled\n");
        return;
    }

    cancel_delayed_work_sync(&asmp_work);
    destroy_workqueue(asmp_workq);

    asmp_nb.notifier_call = 0;
    fb_unregister_client(&asmp_nb);

    for_each_possible_cpu(cpu) {
        if (!cpu_online(cpu))
            asmp_online_cpus(cpu);
    }

    started = false;

    pr_info(ASMP_TAG"disabled\n");
}

static int set_enabled(const char *val,
                 const struct kernel_param *kp)
{
    int ret;

    ret = param_set_bool(val, kp);
    if (asmp_enabled) {

        asmp_start();

    } else {
		
        asmp_stop();

    }
    return ret;
}

static struct kernel_param_ops module_ops = {
    .set = set_enabled,
    .get = param_get_bool,
};

module_param_cb(enabled, &module_ops, &asmp_enabled, 0644);
MODULE_PARM_DESC(enabled, "hotplug/unplug cpu cores based on cpu load");

/***************************** SYSFS START *****************************/

struct global_attr {
    struct attribute attr;
    ssize_t (*show)(struct kobject *, struct attribute *, char *);
    ssize_t (*store)(struct kobject *, struct attribute *, const char *, size_t);
};

#define define_one_global_ro(_name)                 \
static struct global_attr _name =                   \
__ATTR(_name, 0444, show_##_name, NULL)

#define define_one_global_rw(_name)                 \
static struct global_attr _name =                   \
__ATTR(_name, 0644, show_##_name, store_##_name)

struct kobject *asmp_kobject;

#define show_one(file_name, object)                 \
static ssize_t show_##file_name                     \
(struct kobject *kobj, struct attribute *attr, char *buf)       \
{                                   \
    return sprintf(buf, "%u\n", asmp_param.object);         \
}
show_one(delay, delay);
show_one(scroff_single_core, scroff_single_core);
show_one(min_cpus_lc, min_cpus_lc);
show_one(min_cpus_bc, min_cpus_bc);
show_one(min_cpus_pr, min_cpus_pr);
show_one(max_cpus_lc, max_cpus_lc);
show_one(max_cpus_bc, max_cpus_bc);
show_one(max_cpus_pr, max_cpus_pr);
show_one(cpufreq_up_lc, cpufreq_up_lc);
show_one(cpufreq_up_bc, cpufreq_up_bc);
show_one(cpufreq_up_pr, cpufreq_up_pr);
show_one(cpufreq_down_lc, cpufreq_down_lc);
show_one(cpufreq_down_bc, cpufreq_down_bc);
show_one(cpufreq_down_pr, cpufreq_down_pr);
show_one(cycle_up, cycle_up);
show_one(cycle_down, cycle_down);

#define store_one(file_name, object)                    \
static ssize_t store_##file_name                    \
(struct kobject *a, struct attribute *b, const char *buf, size_t count) \
{                                   \
    unsigned int input;                     \
    int ret;                            \
    ret = sscanf(buf, "%u", &input);                \
    if (ret != 1)                           \
        return -EINVAL;                     \
    asmp_param.object = input;                  \
    return count;                           \
}                                   \
define_one_global_rw(file_name);
store_one(delay, delay);
store_one(scroff_single_core, scroff_single_core);
store_one(cpufreq_up_lc, cpufreq_up_lc);
store_one(cpufreq_up_bc, cpufreq_up_bc);
store_one(cpufreq_up_pr, cpufreq_up_pr);
store_one(cpufreq_down_lc, cpufreq_down_lc);
store_one(cpufreq_down_bc, cpufreq_down_bc);
store_one(cpufreq_down_pr, cpufreq_down_pr);
store_one(cycle_up, cycle_up);
store_one(cycle_down, cycle_down);

static ssize_t store_max_cpus_lc(struct kobject *a,
              struct attribute *b, const char *buf, size_t count)
{
    unsigned int input;
    int ret;

    ret = sscanf(buf, "%u", &input);
    if (ret != 1 ||
        input < asmp_param.min_cpus_lc)
        return -EINVAL;

    if (input < 1)
        input = 1;
    else if  (input > 4)
        input = 4;

    asmp_param.max_cpus_lc = input;

    return count;
}

static ssize_t store_max_cpus_bc(struct kobject *a,
              struct attribute *b, const char *buf, size_t count)
{
    unsigned int input;
    int ret;

    ret = sscanf(buf, "%u", &input);
    if (ret != 1 ||
        input < asmp_param.min_cpus_bc)
        return -EINVAL;

    if (input < 1)
        input = 1;
    else if (input > 4)
        input = 4;

    asmp_param.max_cpus_bc = input;

    return count;
}

static ssize_t store_max_cpus_pr(struct kobject *a,
              struct attribute *b, const char *buf, size_t count)
{
    unsigned int input;

        input = 1;

    asmp_param.max_cpus_pr = input;

    return count;
}

static ssize_t store_min_cpus_lc(struct kobject *a,
              struct attribute *b, const char *buf, size_t count)
{
    unsigned int input;
    int ret;

    ret = sscanf(buf, "%u", &input);
    if (ret != 1 ||
        input > asmp_param.max_cpus_lc)
        return -EINVAL;

    if (input < 1)
        input = 1;
    else if (input > 4)
        input = 4;

    asmp_param.min_cpus_lc = input;

    return count;
}

static ssize_t store_min_cpus_bc(struct kobject *a,
              struct attribute *b, const char *buf, size_t count)
{
    unsigned int input;
    int ret;

    ret = sscanf(buf, "%u", &input);
    if (ret != 1 ||
        input > asmp_param.max_cpus_bc)
        return -EINVAL;

    if (input < 1)
        input = 1;
    else if (input > 4)
        input = 4;

    asmp_param.min_cpus_bc = input;

    return count;
}

static ssize_t store_min_cpus_pr(struct kobject *a,
              struct attribute *b, const char *buf, size_t count)
{
    unsigned int input;

        input = 0;

    asmp_param.min_cpus_pr = input;

    return count;
}

define_one_global_rw(min_cpus_lc);
define_one_global_rw(min_cpus_bc);
define_one_global_rw(min_cpus_pr);
define_one_global_rw(max_cpus_lc);
define_one_global_rw(max_cpus_bc);
define_one_global_rw(max_cpus_pr);

static struct attribute *asmp_attributes[] = {
    &delay.attr,
    &scroff_single_core.attr,
    &min_cpus_lc.attr,
    &min_cpus_bc.attr,
    &min_cpus_pr.attr,
    &max_cpus_lc.attr,
    &max_cpus_bc.attr,
    &max_cpus_pr.attr,
    &cpufreq_up_lc.attr,
    &cpufreq_up_bc.attr,
    &cpufreq_up_pr.attr,
    &cpufreq_down_lc.attr,
    &cpufreq_down_bc.attr,
    &cpufreq_down_pr.attr,
    &cycle_up.attr,
    &cycle_down.attr,
    NULL
};

static struct attribute_group asmp_attr_group = {
    .attrs = asmp_attributes,
    .name = "conf",
};

/****************************** SYSFS END ******************************/

static int __init asmp_init(void) {
    int rc = 0;

    asmp_kobject = kobject_create_and_add("autosmp", kernel_kobj);
    if (asmp_kobject) {
        rc = sysfs_create_group(asmp_kobject, &asmp_attr_group);
        if (rc)
            pr_warn(ASMP_TAG"ERROR, create sysfs group");
    } else
        pr_warn(ASMP_TAG"ERROR, create sysfs kobj");

    pr_info(ASMP_TAG"initialized\n");

    return 0;
}
late_initcall(asmp_init);
