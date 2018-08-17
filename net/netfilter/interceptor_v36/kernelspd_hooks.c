/**
   @copyright
   Copyright (c) 2013 - 2017, INSIDE Secure Oy. All rights reserved.
*/

#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/string.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
#include <linux/export.h>
#else
#include <linux/module.h>
#endif

#include "kernelspd_protect.h"
#include "kernelspd_internal.h"

#define HOOK_NAME(hook)                         \
    (hook == NF_INET_LOCAL_IN ? "IN " :         \
     (hook == NF_INET_LOCAL_OUT ? "OUT" :       \
      (hook == NF_INET_FORWARD ? "FWD" :        \
       "???")))

#define RESULT_NAME(result)                                     \
    (result == KERNEL_SPD_DISCARD ? "DISCARD" :                 \
     (result == KERNEL_SPD_BYPASS ? "BYPASS" :                  \
      (result == KERNEL_SPD_PROTECT ? "PROTECT" : "??????")))

#define HOOK_DEBUG(level, result, spdfields_p, fields_p)        \
    DEBUG_## level(                                             \
            "net_id %d: %s: %s %s: %d %s->%s: %s",               \
            (spdfields_p)->net_id,                              \
            RESULT_NAME(result),                                \
            HOOK_NAME((spdfields_p)->hook),                     \
            (spdfields_p)->spd_name,                            \
            __kuid_val((spdfields_p)->packet_kuid),             \
            (spdfields_p)->in_name,                             \
            (spdfields_p)->out_name,                            \
            debug_str_ip_selector_fields(                       \
                    DEBUG_STRBUF_GET(),                         \
                    fields_p,                                   \
                    sizeof *fields_p))

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,0)

#define NF_HOOK_PARAMS                          \
    void *priv,                                 \
    struct sk_buff *skb,                        \
    const struct nf_hook_state *state

#define NF_HOOK_NET_DEVICE_IN state->in
#define NF_HOOK_NET_DEVICE_OUT state->out
#define NF_HOOK_NUMBER state->hook
#define NF_HOOK_OWNER_INIT

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)

#define NF_HOOK_PARAMS                          \
    const struct nf_hook_ops *ops,              \
    struct sk_buff *skb,                        \
    const struct nf_hook_state *state

#define NF_HOOK_NET_DEVICE_IN state->in
#define NF_HOOK_NET_DEVICE_OUT state->out
#define NF_HOOK_NUMBER state->hook
#define NF_HOOK_OWNER_INIT .owner = THIS_MODULE,

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,13,0)

#define NF_HOOK_PARAMS                          \
    const struct nf_hook_ops *ops,              \
    struct sk_buff *skb,                        \
    const struct net_device *dev_in,            \
    const struct net_device *dev_out,           \
    int (*okfn)(struct sk_buff *)

#define NF_HOOK_NET_DEVICE_IN dev_in
#define NF_HOOK_NET_DEVICE_OUT dev_out
#define NF_HOOK_NUMBER ops->hooknum
#define NF_HOOK_OWNER_INIT .owner = THIS_MODULE,

#else

#define NF_HOOK_PARAMS                          \
    unsigned int hook,                          \
    struct sk_buff *skb,                        \
    const struct net_device *dev_in,            \
    const struct net_device *dev_out,           \
    int (*okfn)(struct sk_buff *)

#define NF_HOOK_NET_DEVICE_IN dev_in
#define NF_HOOK_NET_DEVICE_OUT dev_out
#define NF_HOOK_NUMBER hook
#define NF_HOOK_OWNER_INIT .owner = THIS_MODULE,

#endif

enum
{
    HOOKS_STATS_RESULT_BYPASS,
    HOOKS_STATS_RESULT_DISCARD,
    HOOKS_STATS_RESULT_PROTECT,
    HOOKS_STATS_FRAGMENT_DROP,
    HOOKS_STATS_FRAGMENT_STOLEN,
    HOOKS_STATS_COUNT
};

#define HOOKS_STATS_INC(stat)                   \
    kernel_stats_inc(                           \
            kernelspd_hooks_stat,               \
            HOOKS_STATS_ ## stat)

KERNEL_STATS_DEFINE(kernelspd_hooks_stat, HOOKS_STATS_COUNT);

static const char *hooks_stats_strings[HOOKS_STATS_COUNT] =
{
    [HOOKS_STATS_RESULT_BYPASS] = "packets bypassed",
    [HOOKS_STATS_RESULT_DISCARD] = "packets discarded",
    [HOOKS_STATS_RESULT_PROTECT] = "packets protected",
    [HOOKS_STATS_FRAGMENT_DROP] = "packets dropped no fragment hook",
    [HOOKS_STATS_FRAGMENT_STOLEN] = "packets stolen by fragment hook",
};

static bool
protect_hooks_active(
        struct KernelSpdNet *spd_net)
{
    return spd_net->protect_hooks != NULL;
}

static bool
protect_hook(
        struct KernelSpdNet *spd_net,
        struct sk_buff *skb)
{
    const struct KernelSpdProtectHooks *hooks = NULL;
    bool ok = false;

    read_lock(&spd_net->spd_lock);
    if (spd_net->protect_hooks != NULL &&
        spd_net->protect_hooks->protect_hook != NULL)
    {
        hooks = spd_net->protect_hooks;
    }
    read_unlock(&spd_net->spd_lock);

    if (hooks != NULL)
    {
        ok = true;
        hooks->protect_hook(spd_net->protect_param, skb);
    }

    return ok;
}

static void
protect_initial_fragment_hook(
        struct KernelSpdNet *spd_net,
        const struct IPSelectorFields *fields,
        int fragment_id,
        int result)
{
    const struct KernelSpdProtectHooks *hooks = NULL;

    read_lock(&spd_net->spd_lock);
    if (spd_net->protect_hooks != NULL &&
        spd_net->protect_hooks->initial_fragment_hook != NULL)
    {
        hooks = spd_net->protect_hooks;
    }
    read_unlock(&spd_net->spd_lock);

    if (hooks != NULL)
    {
        hooks->initial_fragment_hook(
                spd_net->protect_param,
                fields,
                fragment_id,
                result);
    }
}

static int
protect_other_fragment_hook(
        struct KernelSpdNet *spd_net,
        const struct IPSelectorFields *fields,
        int fragment_id,
        struct sk_buff *skb)
{
    const struct KernelSpdProtectHooks *hooks = NULL;
    int result = -1;

    read_lock(&spd_net->spd_lock);
    if (spd_net->protect_hooks != NULL &&
        spd_net->protect_hooks->other_fragment_hook != NULL)
    {
        hooks = spd_net->protect_hooks;
    }
    read_unlock(&spd_net->spd_lock);

    if (hooks != NULL)
    {
        result =
            hooks->other_fragment_hook(
                    spd_net->protect_param,
                    fields,
                    fragment_id,
                    skb);
    }
    else
    {
        HOOKS_STATS_INC(FRAGMENT_DROP);
        kfree_skb(skb);
    }

    return result;
}

struct KernelSpdFields
{
    int hook;
    kuid_t packet_kuid;
    int spd_id;
    int net_id;
    const char *spd_name;
    const char *in_name;
    const char *out_name;
};

static void
fill_selector_fields_ports(
        struct IPSelectorFields *fields,
        struct udphdr *udph,
        bool non_initial_fragment)
{
    if (udph == NULL || non_initial_fragment == true)
    {
        fields->source_port = IP_SELECTOR_PORT_OPAQUE;
        fields->destination_port = IP_SELECTOR_PORT_OPAQUE;
    }
    else if (fields->ip_protocol == 1 ||
             fields->ip_protocol == 58)
    {
        /* ICMP Type and Code are located in the same place as udp
           source port would. */
        fields->source_port = ntohs(udph->source);

        /* ICMP only used "destination port" in selectors. */
        fields->destination_port = IP_SELECTOR_PORT_NONE;
    }
    else
    {
        fields->source_port = ntohs(udph->source);
        fields->destination_port = ntohs(udph->dest);
    }
}


static void
fill_spd_fields(
        struct KernelSpdNet *spd_net,
        struct KernelSpdFields *spdfields,
        int hook,
        kuid_t packet_kuid,
        const struct net_device *in,
        const struct net_device *out)
{
    bool out_protected = true;
    bool in_protected = true;

    spdfields->hook = hook;
    spdfields->packet_kuid = packet_kuid;
    spdfields->net_id = spd_net->net_id;

    if (in != NULL)
    {
        spdfields->in_name = in->name;
    }

    if (out != NULL)
    {
        spdfields->out_name = out->name;
    }

    if (hook == NF_INET_LOCAL_IN)
    {
        spdfields->out_name = "Local";
    }

    if (hook == NF_INET_LOCAL_OUT)
    {
        spdfields->in_name = "Local";
    }

    if (out != NULL)
    {
        out_protected =
            ipsec_boundary_is_protected_interface(
                    spd_net->ipsec_boundary,
                    spdfields->out_name);
    }

    if (in != NULL)
    {
        in_protected =
            ipsec_boundary_is_protected_interface(
                    spd_net->ipsec_boundary,
                    spdfields->in_name);
    }

    if (in_protected && !out_protected)
    {
        spdfields->spd_name = "SPD-O";
        spdfields->spd_id = KERNEL_SPD_OUT;
    }
    else if (!in_protected && out_protected)
    {
        spdfields->spd_name = "SPD-I";
        spdfields->spd_id = KERNEL_SPD_IN;
    }
    else if (!in_protected && !out_protected)
    {
        spdfields->spd_name = "UNP";
        spdfields->spd_id = -1;
    }
    else
    {
        spdfields->spd_name = "PRO";
        spdfields->spd_id = -1;
    }
}

static int
spd_lookup(
        struct KernelSpdNet *spd_net,
        struct KernelSpdFields *spdfields,
        const struct IPSelectorFields *fields,
        bool non_initial_fragment)
{
    int result;

    if (spdfields->spd_id < 0)
    {
        result = KERNEL_SPD_BYPASS;
    }
    else if (non_initial_fragment)
    {
        spdfields->spd_name = "NIF";
        spdfields->spd_id = -1;

        result = KERNEL_SPD_BYPASS;
    }
    else
    {
        read_lock(&spd_net->spd_lock);

        result =
            ip_selector_db_lookup(
                    &spd_net->spd,
                    spdfields->spd_id,
                    fields);

        read_unlock(&spd_net->spd_lock);
    }

    return result;
}

static int
make_verdict(
        struct KernelSpdNet *spd_net,
        struct sk_buff *skb,
        const struct KernelSpdFields *spdfields,
        struct IPSelectorFields *fields,
        int result)
{
    int verdict;

    switch (result)
    {
    case KERNEL_SPD_BYPASS:
        HOOKS_STATS_INC(RESULT_BYPASS);
        verdict = NF_ACCEPT;
        break;

    case KERNEL_SPD_DISCARD:
        HOOKS_STATS_INC(RESULT_DISCARD);
        verdict = NF_DROP;
        break;

    case KERNEL_SPD_PROTECT:
        if (protect_hook(spd_net, skb) == true)
        {
            verdict = NF_STOLEN;
        }
        else
        {
            verdict = NF_DROP;
        }

        HOOKS_STATS_INC(RESULT_PROTECT);
        break;

    default:
        SPD_NET_DEBUG(
                FAIL,
                spd_net,
                "Unknown spd result %d; dropping packet.",
                result);

        HOOK_DEBUG(FAIL, result, spdfields, fields);
        verdict = NF_DROP;
        break;
    }

    if (verdict == NF_DROP)
    {
        HOOK_DEBUG(HIGH, result, spdfields, fields);
    }
    else if (spdfields->spd_id >= 0)
    {
        HOOK_DEBUG(MEDIUM, result, spdfields, fields);
    }
    else
    {
        HOOK_DEBUG(LOW, result, spdfields, fields);
    }

    return verdict;
}

static int
make_spd_lookup_local_in(
        struct KernelSpdNet *spd_net,
        struct sk_buff *skb,
        const struct net_device *in,
        struct IPSelectorFields *fields,
        bool non_initial_fragment)
{
    struct KernelSpdFields spdfields = { 0 };
    int result;

    fill_spd_fields(
            spd_net,
            &spdfields,
            NF_INET_LOCAL_IN,
            INVALID_UID,
            in,
            NULL);

    result = spd_lookup(spd_net, &spdfields, fields, non_initial_fragment);

    return make_verdict(spd_net, skb, &spdfields, fields, result);
}

static int
make_spd_lookup_local_out(
        struct KernelSpdNet *spd_net,
        struct sk_buff *skb,
        const struct net_device *out,
        struct IPSelectorFields *fields,
        bool non_initial_fragment)
{
    struct KernelSpdFields spdfields = { 0 };
    kuid_t packet_kuid = INVALID_UID;
    int result;

    if (protect_hooks_active(spd_net) == false && skb->sk != NULL &&
        in_interrupt() == 0)
    {
        packet_kuid = sock_i_uid(skb->sk);
    }

    fill_spd_fields(
            spd_net,
            &spdfields,
            NF_INET_LOCAL_OUT,
            packet_kuid,
            NULL,
            out);

    result = spd_lookup(spd_net, &spdfields, fields, non_initial_fragment);

    if (result == KERNEL_SPD_DISCARD)
    {
        if (uid_valid(spdfields.packet_kuid) == true)
        {
            if (uid_eq(spdfields.packet_kuid, spd_net->bypass_kuid) == true)
            {
                spd_proc_new_bypass_packet(spd_net, fields);

                result = KERNEL_SPD_BYPASS;
            }
        }
    }

    return make_verdict(spd_net, skb, &spdfields, fields, result);
}

static int
make_spd_lookup_forward(
        struct KernelSpdNet *spd_net,
        struct sk_buff *skb,
        const struct net_device *in,
        const struct net_device *out,
        struct IPSelectorFields *fields,
        bool non_initial_fragment,
        bool fragment,
        uint32_t fragment_id)
{
    struct KernelSpdFields spdfields = { 0 };
    const int hook = NF_INET_FORWARD;
    int result;

    fill_spd_fields(spd_net, &spdfields, hook, INVALID_UID, in, out);

    if (spdfields.spd_id < 0)
    {
        result = KERNEL_SPD_BYPASS;
    }
    else
    if (protect_hooks_active(spd_net) == false)
    {
        result = KERNEL_SPD_DISCARD;
    }
    else
    if (non_initial_fragment == true)
    {
        result =
            protect_other_fragment_hook(
                    spd_net,
                    fields,
                    fragment_id,
                    skb);

        if (result == -1)
        {
            HOOKS_STATS_INC(FRAGMENT_STOLEN);
            return NF_STOLEN;
        }
    }
    else
    {
        result = spd_lookup(spd_net, &spdfields, fields, non_initial_fragment);

        if (fragment == true)
        {
            protect_initial_fragment_hook(
                    spd_net,
                    fields,
                    fragment_id,
                    result);
        }
    }

    return make_verdict(spd_net, skb, &spdfields, fields, result);
}

static int
make_spd_lookup(
        struct sk_buff *skb,
        int hook,
        const struct net_device *in,
        const struct net_device *out,
        struct IPSelectorFields *fields,
        bool non_initial_fragment,
        bool fragment,
        uint32_t fragment_id)
{
    struct KernelSpdNet *spd_net;
    struct net *skb_net = NULL;
    int verdict;

    if (in != NULL)
    {
        skb_net = dev_net(in);
    }
    else
    if (out != NULL)
    {
        skb_net = dev_net(out);
    }

    spd_net = kernel_spd_net_get(skb_net);
    if (spd_net == NULL || spd_net->active == 0)
    {
        return NF_ACCEPT;
    }

    if (hook == NF_INET_LOCAL_IN)
    {
        verdict =
            make_spd_lookup_local_in(
                    spd_net,
                    skb,
                    in,
                    fields,
                    non_initial_fragment);
    }
    else
    if (hook == NF_INET_LOCAL_OUT)
    {
        verdict =
            make_spd_lookup_local_out(
                    spd_net,
                    skb,
                    out,
                    fields,
                    non_initial_fragment);
    }
    else /* NF_INET_FORWARD */
    {
        verdict =
            make_spd_lookup_forward(
                    spd_net,
                    skb,
                    in,
                    out,
                    fields,
                    non_initial_fragment,
                    fragment,
                    fragment_id);
    }

    return verdict;
}


static unsigned int
hook_ipv4(
        NF_HOOK_PARAMS)
{
    struct IPSelectorFields fields;
    const struct net_device *in = NF_HOOK_NET_DEVICE_IN;
    const struct net_device *out = NF_HOOK_NET_DEVICE_OUT;
    const unsigned int hooknum = NF_HOOK_NUMBER;

    struct iphdr *iph = (struct iphdr *) skb_network_header(skb);
    struct udphdr *udph;
    uint16_t ports[2];
    bool non_initial_fragment;
    bool fragment;
    uint32_t fragment_id;

    if (iph != NULL)
    {
        memset(fields.source_address, 0, 12);
        memset(fields.destination_address, 0, 12);
        memcpy(fields.source_address + 12, &iph->saddr, 4);
        memcpy(fields.destination_address + 12, &iph->daddr, 4);
        fields.ip_protocol = iph->protocol;
        fields.ip_version = 4;

        fragment_id = ntohl(iph->id);
        fragment = ((ntohs(iph->frag_off)) & 0x3fff) != 0;
        non_initial_fragment = (((ntohs(iph->frag_off)) & 0x1fff) != 0);
        udph = skb_header_pointer(skb, (iph->ihl << 2), 4, ports);
    }
    else
    {
        DEBUG_FAIL(
                "Dropping packet: "
                "no network header set for skb in hook %s.",
                HOOK_NAME(hooknum));

        return NF_DROP;
    }

    fill_selector_fields_ports(&fields, udph, non_initial_fragment);

    return
        make_spd_lookup(
                skb,
                hooknum,
                in,
                out,
                &fields,
                non_initial_fragment,
                fragment,
                fragment_id);
}


static void *
parse_ip6_headers(
        struct sk_buff *skb,
        void *tmpbuf,
        bool *non_initial_fragment_p,
        bool *fragment_p,
        uint32_t *fragment_id_p,
        uint8_t *nexthdr_p)
{
    struct ipv6hdr *iph = (struct ipv6hdr *) skb_network_header(skb);
    int offset = 0;
    int nh = iph->nexthdr;
    int hl = sizeof *iph;
    bool skip;

    struct exthdr
    {
        uint8_t nh;
        uint8_t hl;
    }
    *nhp = NULL;

    *non_initial_fragment_p = false;
    *fragment_p = false;
    do
    {
        offset += hl;

        nhp = skb_header_pointer(skb, offset, 4, tmpbuf);

        switch (nh)
        {
        case IPPROTO_HOPOPTS:
        case IPPROTO_ROUTING:
        case IPPROTO_DSTOPTS:
            skip = true;
            break;

        case IPPROTO_FRAGMENT:
            if (nhp != NULL)
            {
                uint32_t *id_p;
                uint32_t frag;

                id_p = skb_header_pointer(skb, offset + 4, 4, tmpbuf);
                if (id_p != NULL)
                {
                    *fragment_id_p = ntohl(*id_p);
                }

                memcpy(&frag, nhp, 4);

                frag = ntohl(frag);

                if ((frag & 0xfff8) != 0)
                {
                    *non_initial_fragment_p = true;
                }

                *fragment_p = true;
            }
            skip = true;
            break;

        default:
            skip = false;
            break;
        }

        if (skip)
        {
            if (nhp != NULL)
            {
                hl = (nhp->hl + 1) * 8;
                nh = nhp->nh;
            }
            else
            {
                skip = false;
            }
        }
    }
    while (skip);

    *nexthdr_p = nh;

    return nhp;
}


static unsigned int
hook_ipv6(
        NF_HOOK_PARAMS)
{
    struct IPSelectorFields fields;
    const struct net_device *in = NF_HOOK_NET_DEVICE_IN;
    const struct net_device *out = NF_HOOK_NET_DEVICE_OUT;
    const unsigned int hooknum = NF_HOOK_NUMBER;

    struct ipv6hdr *iph = (struct ipv6hdr *) skb_network_header(skb);
    struct udphdr *udph;
    uint16_t portsbuf[4];
    bool non_initial_fragment;
    bool fragment;
    uint32_t fragment_id = 0;

    if (iph != NULL)
    {
        uint8_t nexthdr;
        memcpy(fields.source_address, &iph->saddr, 16);
        memcpy(fields.destination_address, &iph->daddr, 16);
        fields.ip_version = 6;

        udph =
            parse_ip6_headers(
                    skb,
                    portsbuf,
                    &non_initial_fragment,
                    &fragment,
                    &fragment_id,
                    &nexthdr);

        fields.ip_protocol = nexthdr;
    }
    else
    {
        DEBUG_FAIL(
                "Dropping packet: "
                "no network header set for skb in hook %s.",
                HOOK_NAME(hooknum));

        return NF_DROP;
    }

    fill_selector_fields_ports(&fields, udph, non_initial_fragment);

    return
        make_spd_lookup(
                skb,
                hooknum,
                in,
                out,
                &fields,
                non_initial_fragment,
                fragment,
                fragment_id);
}

#define HOOK_ENTRY(__hook, __pf, __hooknum)     \
    {                                           \
        .hook = __hook,                         \
        NF_HOOK_OWNER_INIT                      \
        .pf = __pf,                             \
        .hooknum = __hooknum,                   \
        .priority = 1                           \
    }

static struct nf_hook_ops spd_hooks[6] =
{
    HOOK_ENTRY(hook_ipv4, NFPROTO_IPV4, NF_INET_LOCAL_IN),
    HOOK_ENTRY(hook_ipv4, NFPROTO_IPV4, NF_INET_LOCAL_OUT),
    HOOK_ENTRY(hook_ipv4, NFPROTO_IPV4, NF_INET_FORWARD),
    HOOK_ENTRY(hook_ipv6, NFPROTO_IPV6, NF_INET_LOCAL_IN),
    HOOK_ENTRY(hook_ipv6, NFPROTO_IPV6, NF_INET_LOCAL_OUT),
    HOOK_ENTRY(hook_ipv6, NFPROTO_IPV6, NF_INET_FORWARD)
};


void
spd_hooks_init(
        void)
{
    kernel_stats_register(
            "spdhooks",
            hooks_stats_strings,
            kernelspd_hooks_stat,
            HOOKS_STATS_COUNT);
}

static int register_count = 0;

int
spd_hooks_register(
        struct KernelSpdNet *spd_net)
{
    bool do_register = false;
    int result = 0;

    write_lock_bh(&spd_net_lock);

    if (register_count == 0)
    {
        do_register = true;
    }
    ++register_count;

    write_unlock_bh(&spd_net_lock);

    if (do_register == true)
    {
        result = nf_register_hooks(spd_hooks, 6);
        if (result != 0)
        {
            DEBUG_FAIL(
                    "nf_register_hooks() in net %p failed %d.",
                    spd_net->net,
                    result);

            write_lock_bh(&spd_net_lock);
            --register_count;
            write_unlock_bh(&spd_net_lock);
        }
    }

    if (result == 0)
    {
        DEBUG_HIGH("Kernel spd hooks registered in net %p.", spd_net->net);
    }

    return result;
}


void
spd_hooks_unregister(
        struct KernelSpdNet *spd_net)
{
    bool do_unregister = false;

    write_lock_bh(&spd_net_lock);
    register_count--;
    if (register_count == 0)
    {
        do_unregister = true;
    }
    write_unlock_bh(&spd_net_lock);

    if (do_unregister == true)
    {
        nf_unregister_hooks(spd_hooks, 6);
        DEBUG_HIGH("Kernel spd hooks unregistered in net %p.", spd_net->net);
    }
}

int
kernelspd_register_protect_hooks(
        struct net *net,
        const struct KernelSpdProtectHooks *hooks,
        void *param)
{
    struct KernelSpdNet *spd_net;

    write_lock_bh(&spd_net_lock);
    spd_net = __kernel_spd_net_get(net);
    if (spd_net != NULL)
    {
        if (spd_net->protect_hooks != NULL)
        {
            write_unlock_bh(&spd_net_lock);
            return -1;
        }

        spd_net->protect_hooks = hooks;
        spd_net->protect_param = param;
    }

    write_unlock_bh(&spd_net_lock);

    DEBUG_HIGH("Kernel protect hooks on net %p registered to %p.", net, hooks);

    return 0;
}
EXPORT_SYMBOL(kernelspd_register_protect_hooks);

void
kernelspd_unregister_protect_hooks(
        struct net *net)
{
    struct KernelSpdNet *spd_net;

    write_lock_bh(&spd_net_lock);
    spd_net = __kernel_spd_net_get(net);
    if (spd_net != NULL)
    {
        spd_net->protect_hooks = NULL;
        spd_net->protect_param = NULL;
    }

    write_unlock_bh(&spd_net_lock);

    DEBUG_HIGH("Kernel spd protect hooks on net %p unregistered.", net);
}
EXPORT_SYMBOL(kernelspd_unregister_protect_hooks);

int
kernelspd_get_net_id(
        struct net *net)
{
    struct KernelSpdNet *spd_net;
    int net_id = -1;

    spd_net = kernel_spd_net_get(net);
    if (spd_net != NULL)
    {
        net_id = spd_net->net_id;
    }

    return net_id;
}
EXPORT_SYMBOL(kernelspd_get_net_id);
