/**
   @copyright
   Copyright (c) 2017, INSIDE Secure Oy. All rights reserved.
*/

#ifndef KERNEL_STATS_H
#define KERNEL_STATS_H

#include <linux/percpu.h>

#define KERNEL_STATS_DECLARE(stat_name, count) \
    DECLARE_PER_CPU(uint64_t [count], stat_name)

#define KERNEL_STATS_DEFINE(stat_name, count) \
    DEFINE_PER_CPU(uint64_t [count], stat_name)

#define kernel_stats_inc(stat_name, which) \
    this_cpu_inc(stat_name[which])

void
kernel_stats_register(
        const char *counter_prefix,
        const char *counter_names[],
        const uint64_t __percpu counters[],
        int entry_count);

#endif /* KERNEL_STATS_H */

