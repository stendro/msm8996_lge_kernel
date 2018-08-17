/* Copyright (c) 2017 LG Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/init.h>
#include <asm/memory.h>
#include <asm/syscall.h>
#include <asm/sections.h>
#include <soc/qcom/lge/lge_pkm_drv.h>
#include <soc/qcom/lge/board_lge.h>
#include "../../../misc/qseecom_kernel.h"

#define PKM_NAME                     "lge_pkm"
#define CLIENT_CMD11_SET_KERNEL_INFO   11
#define SUCCESS                        0
#define FAILED                         (-1)
#define LGPKI_TZAPP_NAME               "lgpki64"
#define MAX_APP_NAME                   25
#define QSEECOM_SBUFF_SIZE             0x1000
#define TOTAL_DATA_SIZE                322

struct lgpki_send_cmd {
    uint32_t cmd_id;
    uintptr_t kernel_addr;
    uintptr_t kernel_size;
    uintptr_t sct_addr;
    uint8_t data[TOTAL_DATA_SIZE];
};

struct lgpki_send_cmd_rsp {
    int32_t status;
    int32_t errno_type;
    uint8_t data[TOTAL_DATA_SIZE];
};

static struct class *pkm_class;
static struct device *pkm_dev;
static int pkm_major;
static struct qseecom_handle *l_handle;
static DEFINE_MUTEX(lgpki_mutex);
static int pkm_flag;

int32_t lgpki_start(void)
{
    int32_t ret = 0;

    mutex_lock(&lgpki_mutex);
    if(l_handle != NULL) {
        pr_err("[LGE_PKM] already opened qseecom\n");
        ret = -EINVAL;
        goto exit;
    }

    ret = qseecom_start_app(&l_handle, LGPKI_TZAPP_NAME, QSEECOM_SBUFF_SIZE);
    if(ret) {
        pr_err("[LGE_PKM] Failed to load LGPKI qsapp, ret(%d)\n", ret);
    }

exit:
    mutex_unlock(&lgpki_mutex);
    return ret;
}

int32_t lgpki_shutdown(void)
{
    int32_t ret = 0;

    mutex_lock(&lgpki_mutex);
    if(l_handle == NULL) {
        pr_err("[LGE_PKM] qseecom handle is NULL\n");
        ret = -EINVAL;
        goto exit;
    }

    ret = qseecom_shutdown_app(&l_handle);
    if(ret) {
        pr_err("[LGE_PKM] Failed to shutdown LGPKI qsapp, ret(%d)\n", ret);
    }

exit:
    mutex_unlock(&lgpki_mutex);
    return ret;
}

int32_t get_rsp_buffers(struct qseecom_handle *q_handle, void **cmd, int *cmd_len, void **rsp, int *rsp_len)
{
    if ((q_handle == NULL) ||
            (cmd == NULL) || (cmd_len == NULL) ||
            (rsp == NULL) || (rsp_len == NULL)) {
        pr_err("[LGE_PKM] Bad parameters\n");
        return -EINVAL;
    }

    if (*cmd_len & QSEECOM_ALIGN_MASK)
        *cmd_len = QSEECOM_ALIGN(*cmd_len);

    if (*rsp_len & QSEECOM_ALIGN_MASK)
        *rsp_len = QSEECOM_ALIGN(*rsp_len);

    if ((*rsp_len + *cmd_len) > QSEECOM_SBUFF_SIZE) {
        pr_err("[LGE_PKM] Shared buffer too small to hold cmd=%d and rsp=%d\n", *cmd_len, *rsp_len);
        return -ENOMEM;
    }

    *cmd = q_handle->sbuf;
    *rsp = q_handle->sbuf + *cmd_len;

    return SUCCESS;
}

int32_t lgpki_send_req_command(uintptr_t ka, uintptr_t ks, uintptr_t scta)
{
    int32_t ret = 0;
    int32_t req_len = 0;
    int32_t rsp_len = 0;
    struct lgpki_send_cmd *req;
    struct lgpki_send_cmd_rsp *rsp;

    mutex_lock(&lgpki_mutex);

    if(l_handle == NULL) {
        pr_err("[LGE_PKM] already closed qseecom\n");
        ret = -EINVAL;
        goto exit;
    }

    req_len = sizeof(struct lgpki_send_cmd);
    rsp_len = sizeof(struct lgpki_send_cmd_rsp);

    ret = get_rsp_buffers(l_handle, (void **)&req, &req_len, (void **)&rsp, &rsp_len);
    if (!ret) {
        req->cmd_id = CLIENT_CMD11_SET_KERNEL_INFO;
        req->kernel_addr = ka;
        req->kernel_size = ks;
        req->sct_addr = scta;
        ret = qseecom_send_command(l_handle, (void *)req, req_len, (void *)rsp, rsp_len);
        if(ret) {
            pr_err("[LGE_PKM] Failed to send cmd_id, ret(%d)\n", ret);
        }
    }
exit:
    mutex_unlock(&lgpki_mutex);
    return ret;
}

static ssize_t lge_show_pkm_command (struct device *dev,
        struct device_attribute *attr, char *buf)
{
    pr_info("[LGE_PKM] %s: lge_show_pkm_command \n", __func__);
    return sprintf(buf, "%d\n", pkm_flag);
}

static ssize_t lge_store_pkm_command (struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int32_t ret = 0;
    uint32_t cmd = 0;
    phys_addr_t kernel_x_start_rounded;
    phys_addr_t kernel_x_end;
    char phys_s[32] ={0,};
    char phys_e[32] ={0,};
    unsigned long ps;
    unsigned long pe;
    unsigned long ks;
    unsigned long sct_p;

    if (lge_get_boot_mode() != LGE_BOOT_MODE_NORMAL || lge_get_laf_mode() == LGE_LAF_MODE_LAF) {
        pr_err("[LGE_PKM] %s: unsupported mode.\n", __func__);
        return count;
    }

    sscanf(buf, "%d", &cmd);
    pr_info("[LGE_PKM] %s: cmd(%d) \n", __func__, cmd);
    switch(cmd) {
        case CLIENT_CMD11_SET_KERNEL_INFO:
            {
                if (pkm_flag == 0) {
                    kernel_x_start_rounded = round_down(__pa(_stext),PAGE_SIZE);
                    kernel_x_end = round_up(__pa(__end_rodata), PAGE_SIZE);

                    sprintf(phys_s, "%pa\n", &kernel_x_start_rounded);
                    sprintf(phys_e, "%pa\n", &kernel_x_end);
                    sscanf(phys_s, "%lx", &ps);
                    sscanf(phys_e, "%lx", &pe);

                    ks = pe - ps;
                    sct_p = (uintptr_t)(sys_call_table);

                    ret = lgpki_start();
                    if (ret) {
                        pr_err("[LGE_PKM] lgpki_start failed %d\n", ret);
                        break;
                    }
                    ret = lgpki_send_req_command(ps, ks, sct_p);
                    if (ret) {
                        pr_err("[LGE_PKM] lgpki_send_req_command %d\n", ret);
                        break;
                    }
                    ret = lgpki_shutdown();
                    if (ret) {
                        pr_err("[LGE_PKM] lgpki_shutdown failed %d\n", ret);
                        break;
                    }
                    pkm_flag = 1;
                    if (!device_remove_file_self(dev, attr)) {
                        pr_err("[LGE_PKM] remove file failed");
                        break;
                    }
                } else {
                    pr_info("[LGE_PKM] %s: Cannot save baseline measurements, pkm status is (%d) \n", __func__, pkm_flag);
                }
            }
            break;

        default:
            pr_err("[LGE_PKM] Invalid CMD(%d) \n", cmd);
            break;
    }

    memset(phys_s ,0x00, 32);
    memset(phys_e, 0x00, 32);
    ps = 0lu;
    pe = 0lu;
    ks = 0lu;
    sct_p = 0lu;

    return count;
}

static DEVICE_ATTR(pkm_command, 0600, lge_show_pkm_command, lge_store_pkm_command);

static struct attribute *lge_pkm_attrs[] = {
    &dev_attr_pkm_command.attr,
    NULL
};

static const struct attribute_group lge_pkm_files = {
    .attrs  = lge_pkm_attrs,
};

static int __init lge_pkm_probe(struct platform_device *pdev)
{
    int ret = 0;

    pr_info("[LGE_PKM] %s: lge_pkm_probe start\n", __func__);
    pkm_class = class_create(THIS_MODULE, PKM_NAME);
    if (IS_ERR(pkm_class)) {
        pr_err("[LGE_PKM] class_create() failed ENOMEM\n");
        ret = -ENOMEM;
        goto exit;
    }

    pkm_dev = device_create(pkm_class, NULL, MKDEV(pkm_major, 0), NULL, "pkm_ctrl");
    if (IS_ERR(pkm_dev)) {
        pr_err("[LGE_PKM] device_create() failed\n");
        ret = PTR_ERR(pkm_dev);
        goto exit;
    }

    ret = device_create_file(pkm_dev, &dev_attr_pkm_command);
    if (ret < 0) {
        pr_err("[LGE_PKM] device create file fail\n");
        goto exit;
    }

    pr_info("[LGE_PKM] %s: lge_pkm_probe done\n", __func__);
    return SUCCESS;
exit:
    pr_err("[LGE_PKM] %s: probe fail - %d\n", __func__, ret);
    return ret;
}

static int lge_pkm_remove(struct platform_device *pdev)
{
    pr_info("[LGE_PKM] %s: lge_pkm_remove \n", __func__);
    return SUCCESS;
}

static struct of_device_id pkm_match_table[] = {
    { .compatible = "lge-pkm-drv",},
    {},
};

static struct platform_driver lge_pkm_driver __refdata = {
    .probe = lge_pkm_probe,
    .remove = lge_pkm_remove,
    .driver = {
        .name = "lge_pkm_drv",
        .owner = THIS_MODULE,
        .of_match_table = pkm_match_table,
    },
};

static int __init lge_pkm_init(void)
{
    int ret = 0;

    pr_info("[LGE_PKM] %s: lge_pkm_init \n", __func__);
    ret = platform_driver_register(&lge_pkm_driver);
    if (ret < 0)
        pr_err("[LGE_PKM] platform_driver_register() err=%d\n", ret);

    pkm_flag = 0;

    return ret;
}

static void __exit lge_pkm_exit(void)
{
    pr_info("[LGE_PKM] %s: lge_pkm_exit \n", __func__);
    platform_driver_unregister(&lge_pkm_driver);
}

module_init(lge_pkm_init);
module_exit(lge_pkm_exit);

MODULE_DESCRIPTION("LGE PKM kernel driver for Kernel Monitoring");
MODULE_AUTHOR("minhyun.son <minhyun.son@lge.com>");
MODULE_LICENSE("GPL");
