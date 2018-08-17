/**
   @copyright
   Copyright (c) 2017, INSIDE Secure Oy. All rights reserved.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "implementation_defs.h"
#include "kernel_stats.h"


struct KernelStatsEntry
{
    const char *counter_prefix;
    const uint64_t __percpu *counter_table;
    const char **counter_name_table;
    int counter_count;
};

#define KERNEL_STATS_ENTRY_COUNT 8

static struct KernelStatsEntry stats_entries[KERNEL_STATS_ENTRY_COUNT];

static void
kernel_stats_per_cpu(
        const struct KernelStatsEntry *entry)
{
    const char **names = entry->counter_name_table;
    const uint64_t __percpu *counters = entry->counter_table;
    const int count = entry->counter_count;
    int counter;

    for (counter = 0; counter < count; counter++)
    {
        int cpu;
        uint64_t sum = 0;

        for_each_possible_cpu(cpu)
        {
            uint64_t this_value = per_cpu_ptr(counters, cpu)[counter];
            sum += this_value;
        }

        if (names[counter] != NULL)
        {
            printk(
                    KERN_NOTICE "%s: %s value %llu",
                    entry->counter_prefix,
                    names[counter],
                    sum);
        }
    }
}

void
kernel_stats_register(
        const char *counter_prefix,
        const char *counter_names[],
        const uint64_t __percpu counters[],
        int counter_count)
{
    int i;

    for (i = 0; i < KERNEL_STATS_ENTRY_COUNT; i++)
    {
        struct KernelStatsEntry *entry = &stats_entries[i];

        if (entry->counter_table == counters)
        {
            /* Already registered. */
            return;
        }

        if (entry->counter_count == 0)
        {
            entry->counter_prefix = counter_prefix;
            entry->counter_table = counters;
            entry->counter_name_table = counter_names;
            entry->counter_count = counter_count;

            return;
        }
    }

    DEBUG_FAIL("kernel stats table full!");
}


static char stats_param_string[2] = "";
static struct kparam_string kps =
{
    .string                 = stats_param_string,
    .maxlen                 = 1,
};

static int
stats_param_call(
        const char *arg,
        struct kernel_param *kp)
{
    int i;

    for (i = 0; i <  KERNEL_STATS_ENTRY_COUNT; i++)
    {
        const struct KernelStatsEntry *entry = &stats_entries[i];

        if (entry->counter_count != 0)
        {
            kernel_stats_per_cpu(entry);
        }
    }

    return 0;
}

module_param_call(
        stats_param_string,
        stats_param_call,
        param_get_string,
        &kps,
        0644);

MODULE_PARM_DESC(
        stats_param_string,
        "Trigger statistics value dump to dmesg.");
