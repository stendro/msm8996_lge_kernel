/**
   @copyright
   Copyright (c) 2013 - 2017, INSIDE Secure Oy. All rights reserved.
*/

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/nsproxy.h>
#include <linux/netdevice.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#include <linux/cred.h>
#include <linux/proc_fs.h>
#include <linux/uidgid.h>
#endif

#include "kernelspd_internal.h"

#define KERNELSPD_PROCFS_COMMAND_BYTECOUNT_MAX 0x7fffffff
#define KERNELSPD_IPSEC_BOUNDARY_LENGTH_MAX (10*1024)

static struct proc_dir_entry *spd_control_file;
static int initialised = 0;

void
spd_proc_new_bypass_packet(
        struct KernelSpdNet *spd_net,
        const struct IPSelectorFields *fields)
{
    write_lock(&spd_net->spd_proc_lock);

    spd_net->bypass_packet_fields = *fields;
    spd_net->bypass_packet_set = true;

    write_unlock(&spd_net->spd_proc_lock);

    wake_up_interruptible(&spd_net->wait_queue);
}

static int
spd_proc_open(
        struct inode *inode,
        struct file *file)
{
    struct net *net = current->nsproxy->net_ns;
    struct KernelSpdNet *spd_net;

    if (!try_module_get(THIS_MODULE))
    {
        DEBUG_FAIL("Kernel module being removed.");
        return -EFAULT;
    }

    spd_net = vmalloc(sizeof *spd_net);
    if (spd_net == NULL)
    {
        DEBUG_FAIL("No memory.");

        module_put(THIS_MODULE);
        return -EFAULT;
    }

    memset(spd_net, 0, sizeof *spd_net);
    rwlock_init(&spd_net->spd_lock);
    ip_selector_db_init(&spd_net->spd);

    init_waitqueue_head(&spd_net->wait_queue);
    rwlock_init(&spd_net->spd_proc_lock);
    spd_net->net = net;
    spd_net->net_id = -1;
    spd_net->bypass_kuid = INVALID_UID;

    write_lock_bh(&spd_net_lock);
    if (__kernel_spd_net_get(net) != NULL)
    {
        write_unlock_bh(&spd_net_lock);

        vfree(spd_net);

        DEBUG_FAIL("Kernel SPD device already open for net %p.", net);

        module_put(THIS_MODULE);
        return -EFAULT;
    }

    spd_net->next = kernel_spd_net_head;
    kernel_spd_net_head = spd_net;
    write_unlock_bh(&spd_net_lock);

    file->private_data = spd_net;

    DEBUG_HIGH("Kernel SPD device opened file %p for net %p.", file, net);

    return 0;
}

static int
update_ipsec_boundary(
        struct KernelSpdNet *spd_net,
        const char __user *user_boundary,
        size_t user_boundary_bytecount)
{
    char *new_ipsec_boundary = NULL;
    unsigned new_ipsec_boundary_bytecount = user_boundary_bytecount;
    int status;

    if (new_ipsec_boundary_bytecount == 0)
    {
        SPD_NET_DEBUG(FAIL, spd_net, "IPsec boundary needed.");
        return -EFAULT;
    }

    if (new_ipsec_boundary_bytecount > KERNELSPD_IPSEC_BOUNDARY_LENGTH_MAX)
    {
        SPD_NET_DEBUG(
                FAIL,
                spd_net,
                "IPsec boundary too long %u.",
                new_ipsec_boundary_bytecount);

        return -EFAULT;
    }

    new_ipsec_boundary = vmalloc(new_ipsec_boundary_bytecount + 1);
    if (new_ipsec_boundary == NULL)
    {
        SPD_NET_DEBUG(FAIL, spd_net, "vmalloc() failed.");
        return -EFAULT;
    }

    status =
        copy_from_user(
                new_ipsec_boundary,
                user_boundary,
                new_ipsec_boundary_bytecount);
    if (status != 0)
    {
        SPD_NET_DEBUG(FAIL, spd_net, "Copy from user failed.");
        vfree(new_ipsec_boundary);
        return -EFAULT;
    }

    new_ipsec_boundary[new_ipsec_boundary_bytecount] = '\0';
    if (ipsec_boundary_is_valid_spec(new_ipsec_boundary) == false)
    {
        SPD_NET_DEBUG(
                FAIL,
                spd_net,
                "IPsec boundary '%s' invalid.",
                new_ipsec_boundary);
        vfree(new_ipsec_boundary);
        return -EFAULT;
    }

    {
        char *old_boundary;

        write_lock_bh(&spd_net->spd_lock);

        old_boundary = spd_net->ipsec_boundary;
        spd_net->ipsec_boundary = new_ipsec_boundary;

        write_unlock_bh(&spd_net->spd_lock);

        if (old_boundary != NULL)
        {
            vfree(old_boundary);
        }
    }

    return 0;
}


int
process_command(
        struct KernelSpdNet *spd_net,
        struct KernelSpdCommand *cmd,
        const char __user *cmd_data,
        size_t cmd_data_bytecount)

{
    int status = 0;

    SPD_NET_DEBUG(LOW, spd_net, "Processing command id %d.", cmd->command_id);

    switch (cmd->command_id)
    {
    case KERNEL_SPD_ACTIVATE:
        {
            int ipsec_boundary_bytecount = cmd_data_bytecount;

            if (spd_net->active != 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Kernel SPD already active.");
                status = -EFAULT;
            }
            else
            {
                status =
                    update_ipsec_boundary(
                            spd_net,
                            cmd_data,
                            ipsec_boundary_bytecount);

                if (status != 0)
                {
                    break;
                }

                if (spd_hooks_register(spd_net) != 0)
                {
                    SPD_NET_DEBUG(
                            FAIL,
                            spd_net,
                            "Kernel SPD Failed activating NF Hooks.");
                    status = -EFAULT;
                }
                else
                {
                    spd_net->active = 1;
                }

                SPD_NET_DEBUG(
                        HIGH,
                        spd_net,
                        "Kernel SPD activated. IPsec boundary: '%s'.",
                        spd_net->ipsec_boundary);
            }
        }
        break;

    case KERNEL_SPD_DEACTIVATE:
        {
            if (spd_net->active == 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Kernel SPD not active.");
            }
            else
            {
                SPD_NET_DEBUG(HIGH, spd_net, "Kernel SPD deactivated.");
                spd_hooks_unregister(spd_net);
                spd_net->bypass_kuid = INVALID_UID;
            }

            spd_net->active = 0;
        }
        break;

    case KERNEL_SPD_INSERT_ENTRY:
        {
            struct IPSelectorDbEntry *entry;
            const int payload_bytecount = cmd_data_bytecount;

            if (!KERNEL_SPD_ID_VALID(cmd->spd_id))
            {
                SPD_NET_DEBUG(
                        FAIL,
                        spd_net,
                        "Invalid SPD id %d.",
                        cmd->spd_id);

                status = -EFAULT;
                break;
            }

            entry = vmalloc(sizeof *entry + payload_bytecount);
            if (entry == NULL)
            {
                SPD_NET_DEBUG(
                        FAIL,
                        spd_net,
                        "vmalloc(%d) failed.",
                        (int) (sizeof *entry + payload_bytecount));

                status = -EFAULT;
                break;
            }

            status = copy_from_user(entry + 1, cmd_data, payload_bytecount);
            if (status != 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Copy from user failed.");

                vfree(entry);
                status = -EFAULT;
                break;
            }

            entry->action = cmd->action_id;
            entry->id = cmd->entry_id;
            entry->priority = cmd->priority;

            if (ip_selector_db_entry_check(
                        entry,
                        sizeof *entry + payload_bytecount)
                < 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Selector check failed.");

                vfree(entry);
                status = -EFAULT;
                break;
            }

            SPD_NET_DEBUG_DUMP(
                    spd_net,
                    debug_dump_ip_selector_group,
                    entry + 1,
                    payload_bytecount,
                    "Insert entry %d to spd "
                    "id %d action %d priority %d precedence %d:",
                    entry->id,
                    cmd->spd_id,
                    entry->action,
                    entry->priority,
                    cmd->precedence);

            write_lock_bh(&spd_net->spd_lock);
            ip_selector_db_entry_add(
                    &spd_net->spd,
                    cmd->spd_id,
                    entry,
                    cmd->precedence);
            write_unlock_bh(&spd_net->spd_lock);
        }
        break;

    case KERNEL_SPD_REMOVE_ENTRY:

        if (!KERNEL_SPD_ID_VALID(cmd->spd_id))
        {
            SPD_NET_DEBUG(FAIL, spd_net, "Invalid SPD id %d.", cmd->spd_id);

            status = -EFAULT;
            break;
        }

        {
            struct IPSelectorDbEntry *removed;

            write_lock_bh(&spd_net->spd_lock);
            removed =
                ip_selector_db_entry_remove(
                        &spd_net->spd,
                        cmd->spd_id,
                        cmd->entry_id);

            write_unlock_bh(&spd_net->spd_lock);

            if (removed != NULL)
            {
                SPD_NET_DEBUG_DUMP(
                        spd_net,
                        debug_dump_ip_selector_group,
                        removed + 1,
                        -1,
                        "Removed entry %d to spd id %d action %d "
                        "priority %d:",
                        removed->id,
                        cmd->spd_id,
                        removed->action,
                        removed->priority);

                vfree(removed);
            }
            else
            {
                SPD_NET_DEBUG(
                        FAIL,
                        spd_net,
                        "Remove failed: Entry %d not found from spd id %d.",
                        cmd->entry_id,
                        cmd->spd_id);
            }
        }

        break;

    case KERNEL_SPD_UPDATE_IPSEC_BOUNDARY:
        {
            int new_ipsec_boundary_bytecount = cmd_data_bytecount;

            if (spd_net->active == 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Kernel SPD is not active.");
                return -EFAULT;
            }

            status =
                update_ipsec_boundary(
                        spd_net,
                        cmd_data,
                        new_ipsec_boundary_bytecount);

            if (status != 0)
            {
                break;
            }

            SPD_NET_DEBUG(
                    HIGH,
                    spd_net,
                    "IPsec boundary updated: '%s'.",
                    spd_net->ipsec_boundary);
        }
        break;

    case KERNEL_SPD_VERSION_SYNC:
        {
            int version_bytecount = cmd_data_bytecount;
            uint32_t version;

            if (version_bytecount != sizeof version)
            {
                SPD_NET_DEBUG(
                        FAIL,
                        spd_net,
                        "Invalid version size %d; should be %d.",
                        version_bytecount,
                        (int) sizeof version);
                return -EFAULT;
            }

            status =
                copy_from_user(
                        &version,
                        cmd_data,
                        sizeof version);
            if (status != 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Copy from user failed.");
                return -EFAULT;
            }

            if (version != KERNEL_SPD_VERSION)
            {
                SPD_NET_DEBUG(
                        FAIL,
                        spd_net,
                        "Invalid version %d; should be %d.",
                        version,
                        KERNEL_SPD_VERSION);

                return -EINVAL;
            }

            DEBUG_HIGH(
                    "Versions in sync for net %p: %d.",
                    spd_net->net,
                    version);
        }
        break;

    case KERNEL_SPD_ADD_BYPASS_UID:
        {
            uint32_t uid;

            if (spd_net->active == 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Kernel SPD is not active.");
                return -EFAULT;
            }

            status =
                copy_from_user(
                        &uid,
                        cmd_data,
                        sizeof uid);
            if (status != 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Copy from user failed.");
                return -EFAULT;
            }

            write_lock_bh(&spd_net->spd_lock);
            spd_net->bypass_kuid = make_kuid(current_user_ns(), (uid_t) uid);
            write_unlock_bh(&spd_net->spd_lock);

            SPD_NET_DEBUG(HIGH, spd_net, "Set bypass uid to %u.", uid);
        }
        break;

    case KERNEL_SPD_SET_NET_ID:
        {
            uint32_t id;

            if (spd_net->active != 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Kernel SPD is already active.");
                return -EFAULT;
            }

            status = copy_from_user(&id, cmd_data, sizeof id);
            if (status != 0)
            {
                SPD_NET_DEBUG(FAIL, spd_net, "Copy from user failed.");
                return -EFAULT;
            }

            write_lock_bh(&spd_net->spd_lock);
            spd_net->net_id = id;
            write_unlock_bh(&spd_net->spd_lock);

            SPD_NET_DEBUG(
                    HIGH,
                    spd_net,
                    "Set net_id set for net %p.",
                    spd_net->net);
        }
        break;

    default:
        SPD_NET_DEBUG(
                FAIL,
                spd_net,
                "Unknown command id %d.",
                cmd->command_id);
        break;
    }

    SPD_NET_DEBUG(LOW, spd_net, "Returning %d", status);

    return status;
}


static ssize_t
spd_proc_write(
        struct file *file,
        const char __user *data,
        size_t data_len,
        loff_t *pos)
{
    struct KernelSpdNet *spd_net = file->private_data;
    size_t bytes_read = 0;

    SPD_NET_DEBUG(LOW, spd_net, "Write of %d bytes.", (int) data_len);
    while (bytes_read < data_len)
    {
        struct KernelSpdCommand command;
        int status;

        if (data_len < sizeof command)
        {
            SPD_NET_DEBUG(
                    FAIL,
                    spd_net,
                    "Data length %d less than sizeof command %d bytes.",
                    (int) data_len,
                    (int) sizeof command);

            bytes_read = -EFAULT;
            break;
        }

        status = copy_from_user(&command, data, sizeof command);
        if (status != 0)
        {
            SPD_NET_DEBUG(FAIL, spd_net, "Copy from user failed.");
            bytes_read = -EFAULT;
            break;
        }

        if (command.bytecount < sizeof command)
        {
            SPD_NET_DEBUG(
                    FAIL,
                    spd_net,
                    "Command bytecount %d less than command size %d.",
                    (int) command.bytecount,
                    (int) sizeof command);

            bytes_read = -EINVAL;
            break;
        }

        if (command.bytecount > KERNELSPD_PROCFS_COMMAND_BYTECOUNT_MAX)
        {
            SPD_NET_DEBUG(
                    FAIL,
                    spd_net,
                    "Command bytecount %d bigger than max command size %d.",
                    (int) command.bytecount,
                    (int) KERNELSPD_PROCFS_COMMAND_BYTECOUNT_MAX);

            bytes_read = -EINVAL;
            break;
        }

        if (command.bytecount > data_len - bytes_read)
        {
            SPD_NET_DEBUG(
                    FAIL,
                    spd_net,
                    "Command bytecount %d bigger than data_len %d.",
                    (int) command.bytecount,
                    (int) data_len);

            bytes_read = -EINVAL;
            break;
        }

        bytes_read += sizeof command;

        status =
            process_command(
                    spd_net,
                    &command,
                    data + bytes_read,
                    command.bytecount - sizeof command);

        if (status < 0)
        {
            bytes_read = status;
            break;
        }

        bytes_read += command.bytecount;
    }

    return bytes_read;
}


static void
spd_proc_cleanup_selectors(
        struct KernelSpdNet *spd_net)
{
    for (;;)
    {
        struct IPSelectorDbEntry *entry;

        entry = ip_selector_db_entry_remove_next(&spd_net->spd);

        if (entry != NULL)
        {
            vfree(entry);
        }
        else
        {
            break;
        }
    }
}


static int
spd_proc_release(
        struct inode *inode,
        struct file *file)
{
    struct KernelSpdNet *spd_net = file->private_data;

    DEBUG_HIGH(
            "Kernel SPD device closed file %p for net %p.",
            file,
            spd_net->net);

    module_put(THIS_MODULE);

    write_lock_bh(&spd_net_lock);
    {
        struct KernelSpdNet **spd_net_pp = &kernel_spd_net_head;

        while (*spd_net_pp != NULL && *spd_net_pp != spd_net)
        {
            spd_net_pp = &( (*spd_net_pp)->next );
        }

        if (*spd_net_pp == spd_net)
        {
            *spd_net_pp = spd_net->next;
        }
    }
    write_unlock_bh(&spd_net_lock);

    synchronize_net();

    spd_proc_cleanup_selectors(spd_net);

    if (spd_net->ipsec_boundary != NULL)
    {
        vfree(spd_net->ipsec_boundary);
        spd_net->ipsec_boundary = NULL;
    }

    if (spd_net->active != 0)
    {
        spd_hooks_unregister(spd_net);
    }

    vfree(spd_net);

    return 0;
}


static ssize_t
spd_proc_read(
        struct file *file,
        char __user *buf,
        size_t len,
        loff_t *pos)
{
    struct KernelSpdNet *spd_net = file->private_data;
    size_t read_len = 0;
    const size_t fields_size = sizeof spd_net->bypass_packet_fields;

    write_lock_bh(&spd_net->spd_proc_lock);

    if (spd_net->bypass_packet_set == true)
    {
        int status = 0;

        struct KernelSpdCommand command = { 0 };

        command.command_id = KERNEL_SPD_EVENT_BYPASS_FIELDS;
        command.bytecount = sizeof command + fields_size;

        if (len < command.bytecount)
        {
            status = -EFAULT;
        }

        if (status == 0)
        {
            status = copy_to_user(buf, &command, sizeof command);
        }

        if (status == 0)
        {
            status =
                copy_to_user(
                        buf + sizeof command,
                        &spd_net->bypass_packet_fields,
                        fields_size);
        }

        if (status != 0)
        {
            read_len = status;
        }
        else
        {
            read_len = command.bytecount;
            *pos = read_len;
        }

        spd_net->bypass_packet_set = false;
    }
    else if (file->f_flags & O_NONBLOCK)
    {
        read_len = -EAGAIN;
    }

    write_unlock_bh(&spd_net->spd_proc_lock);

    return read_len;
}

static unsigned int
spd_proc_poll(
        struct file *file,
        struct poll_table_struct *table)
{
    struct KernelSpdNet *spd_net = file->private_data;
    unsigned int mask = 0;

    poll_wait(file, &spd_net->wait_queue, table);

    write_lock_bh(&spd_net->spd_proc_lock);

    if (spd_net->bypass_packet_set == true)
    {
        mask |= (POLLIN | POLLRDNORM);
    }

    write_unlock_bh(&spd_net->spd_proc_lock);

    return mask;
}

static const struct file_operations spd_proc_fops =
{
    .open = spd_proc_open,
    .write = spd_proc_write,
    .release = spd_proc_release,
    .read = spd_proc_read,
    .poll = spd_proc_poll
};

void spd_proc_set_ids(struct proc_dir_entry *proc_entry)
{
    uid_t uid = 0;
    gid_t gid = 0;

#ifdef CONFIG_VPNCLIENT_PROC_UID
    uid = CONFIG_VPNCLIENT_PROC_UID;
#endif

#ifdef CONFIG_VPNCLIENT_PROC_GID
    gid = CONFIG_VPNCLIENT_PROC_GID;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
    proc_entry->uid = uid;
    proc_entry->gid = gid;
#else
    proc_set_user(proc_entry,
                  make_kuid(current_user_ns(), uid),
                  make_kgid(current_user_ns(), gid));
#endif
}

int
spd_proc_init(
        void)
{
    spd_control_file =
        proc_create(
                LINUX_SPD_PROC_FILENAME,
                S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP,
                NULL, &spd_proc_fops);

    if (spd_control_file == NULL)
    {
        DEBUG_FAIL(
                "Failure creating proc entry %s.",
                LINUX_SPD_PROC_FILENAME);

        return -1;
    }

    spd_proc_set_ids(spd_control_file);

    initialised = 1;

    DEBUG_MEDIUM(
            "Created proc entry %s.",
            LINUX_SPD_PROC_FILENAME);

    return 0;
}


void
spd_proc_uninit(
        void)
{
    if (initialised != 0)
    {
        DEBUG_MEDIUM(
                "Removing proc entry %s.",
                LINUX_SPD_PROC_FILENAME);

        DEBUG_HIGH("Kernel SPD deactivated.");

        remove_proc_entry(LINUX_SPD_PROC_FILENAME, NULL);

        initialised = 0;
    }
    else
    {
        DEBUG_FAIL("Already uninitialised.");
    }
}
