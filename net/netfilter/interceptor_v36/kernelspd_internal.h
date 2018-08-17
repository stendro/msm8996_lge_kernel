/**
   @copyright
   Copyright (c) 2013 - 2017, INSIDE Secure Oy. All rights reserved.
*/


#ifndef KERNELSPD_INTERNAL_H
#define KERNELSPD_INTERNAL_H

#include "kernelspd_command.h"
#include "ip_selector_db.h"
#include "ipsec_boundary.h"
#include "kernel_stats.h"
#include "kernelspd_protect.h"

#include <linux/version.h>
#include "implementation_defs.h"

#define __DEBUG_MODULE__ kernelspdlinux

#define SPD_NET_DEBUG(level, spd_net, ...)                              \
    DEBUG_ ## level(                                                    \
            "net_id %d: "                                               \
            DEBUG_FIRST_ARG(__VA_ARGS__) "%s",                          \
            (spd_net)->net_id,                                          \
            DEBUG_REST_ARGS(__VA_ARGS__, ""))

#define SPD_NET_DEBUG_DUMP(spd_net, ...)                                \
    DEBUG_DUMP(                                                         \
            DEBUG_DUMP_INIT_ARGS(__VA_ARGS__),                          \
            "net_id %d: "                                               \
            DEBUG_DUMP_FORMAT_ARG(__VA_ARGS__) "%s",                    \
            (spd_net)->net_id,                                          \
            DEBUG_DUMP_REST_ARGS(__VA_ARGS__, ""))

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
typedef int kuid_t;
#define INVALID_UID -1
#define __kuid_val(val) (val)
#define make_kuid(x, uid) (uid)
#define uid_valid(uid) ((uid) != INVALID_UID)
#define uid_eq(uid1, uid2) ((uid1) == (uid2))
#endif

struct KernelSpdNet
{
    int net_id;

    struct net *net;

    kuid_t bypass_kuid;

    char *ipsec_boundary;

    struct IPSelectorDb spd;

    rwlock_t spd_lock;

    int active;

    wait_queue_head_t wait_queue;
    rwlock_t spd_proc_lock;

    struct IPSelectorFields bypass_packet_fields;
    bool bypass_packet_set;

    const struct KernelSpdProtectHooks *protect_hooks;
    void *protect_param;

    struct KernelSpdNet *next;
};

extern struct KernelSpdNet *kernel_spd_net_head;
extern rwlock_t spd_net_lock;

void
spd_hooks_init(
        void);

int
spd_hooks_register(
        struct KernelSpdNet *spd_net);

void
spd_hooks_unregister(
        struct KernelSpdNet *spd_net);

int
spd_proc_init(
        void);

void
spd_proc_uninit(
        void);

void
spd_proc_new_bypass_packet(
        struct KernelSpdNet *spd_net,
        const struct IPSelectorFields *fields);

static inline struct KernelSpdNet *
__kernel_spd_net_get(
        struct net *net)
{
    struct KernelSpdNet *spd_net;

    spd_net = kernel_spd_net_head;
    while (spd_net != NULL && spd_net->net != net)
    {
        spd_net = spd_net->next;
    }

    return spd_net;
}

static inline struct KernelSpdNet *
kernel_spd_net_get(
        struct net *net)
{
    struct KernelSpdNet *spd_net;

    read_lock(&spd_net_lock);
    spd_net = __kernel_spd_net_get(net);
    read_unlock(&spd_net_lock);

    return spd_net;
}

#endif /* KERNELSPD_INTERNAL_H */
