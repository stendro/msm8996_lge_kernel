/* linux/drivers/usb/gadget/u_lgeusb.c
 *
 * Copyright (C) 2011,2012 LG Electronics Inc.
 * Author : Hyeon H. Park <hyunhui.park@lge.com>
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/spmi.h>

#ifdef CONFIG_MACH_LGE
#include <soc/qcom/lge/board_lge.h>
#endif

#include <linux/platform_data/lge_android_usb.h>
#include <linux/qpnp/qpnp-adc.h>

#include "u_lgeusb.h"

#if defined(CONFIG_LGE_USB_DIAG_LOCK_SPR)
#include <soc/qcom/smem.h>
#endif

static struct mutex lgeusb_lock;

/* This length must be same as MAX_STR_LEN in android.c */
#define MAX_SERIAL_NO_LEN 256

#define LGE_VENDOR_ID 0x1004
#define LGE_PRODUCT_ID 0x618E
#define LGE_FACTORY_PID 0x6000

/* PMIC USB CHGPTH register */
#define SMBB_USB_CHGPTH_BASE                0x1300
#define SMBB_USB_CHGPTH_USB_CHG_PTH_STS     (SMBB_USB_CHGPTH_BASE + 0x09)
#define SMBB_USB_CHGPTH_INT_RT_STS          (SMBB_USB_CHGPTH_BASE + 0x10)
#define SMBB_DC_CHGPATH_BASE                0x1400
#define SMBB_DC_CHGPATH_DC_CHG_PTH_STS      (SMBB_DC_CHGPATH_BASE + 0x0A)
#define SMBB_DC_CHGPATH_INT_RT_STS          (SMBB_DC_CHGPATH_BASE + 0x10)

struct lgeusb_dev {
	struct device *dev;
	u16 vendor_id;
	u16 factory_pid;
	u8  iSerialNumber;
	const char *product;
	const char *manufacturer;
	const char *fcomposition;
	enum lgeusb_mode current_mode;

	int (*get_serial_number)(char *serial);
	int (*get_factory_cable)(void);
};

static char model_string[32];
static char swver_string[32];
static char subver_string[32];
static char phoneid_string[32];

#ifdef CONFIG_LGE_USB_G_MULTIPLE_CONFIGURATION
static bool is_mac_os;
#endif

static struct lgeusb_dev *_lgeusb_dev;

int debug_pmic_register_for_usb(void)
{
	int rc;
	u8 usb_sts_reg, usb_rt_reg;
	u8 usb_valid;

	struct spmi_controller *ctrl = spmi_busnum_to_ctrl(0);

	if (!ctrl) {
		pr_err("Controller is null!\n");
		return -EINVAL;
	}

	/* read SMBB_USB_CHGPTH_USB_CHG_PTH_STS,
	 * SMBB_USB_CHGPTH_INT_RT_STS registers
	 */
	rc = spmi_ext_register_readl(ctrl, 0, SMBB_USB_CHGPTH_USB_CHG_PTH_STS,
			&usb_sts_reg, 1);
	if (rc) {
		pr_err("[%s] spmi read failed-rc:%d , addr:0x%x", __func__,
				rc, SMBB_USB_CHGPTH_USB_CHG_PTH_STS);
		return -EINVAL;
	}
	usb_valid = ((usb_sts_reg & 0xC0) >> 6);

	rc = spmi_ext_register_readl(ctrl, 0, SMBB_USB_CHGPTH_INT_RT_STS,
			&usb_rt_reg, 1);
	if (rc) {
		pr_err("[%s] spmi read failed-rc:%d ,addr:0x%x", __func__,
				rc, SMBB_USB_CHGPTH_INT_RT_STS);
		return -EINVAL;
	}

	pr_info("[SMBB_USB_CHGPTH]sts:0x%x, usb_valid:0x%x, rt:0x%x\n",
			usb_sts_reg, usb_valid, usb_rt_reg);

	return rc;
}
EXPORT_SYMBOL(debug_pmic_register_for_usb);

/* Belows are borrowed from android gadget's ATTR macros ;) */
#define LGE_ID_ATTR(field, format_string)               \
static ssize_t                              \
lgeusb_ ## field ## _show(struct device *dev, struct device_attribute *attr, \
		char *buf)                      \
{                                   \
	struct lgeusb_dev *usbdev = _lgeusb_dev; \
	return snprintf(buf, PAGE_SIZE, format_string, usbdev->field);      \
}                                   \
static ssize_t                              \
lgeusb_ ## field ## _store(struct device *dev, struct device_attribute *attr, \
		const char *buf, size_t size)                   \
{                                   \
	unsigned int value;                              \
	struct lgeusb_dev *usbdev = _lgeusb_dev; \
	if (sscanf(buf, format_string, &value) == 1) {          \
		usbdev->field = value;              \
		return size;                        \
	}                               \
	return -EINVAL;                          \
}                                   \
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, lgeusb_ ## field ## _show, \
		lgeusb_ ## field ## _store);

#define LGE_RDONLY_STRING_ATTR(field, string)               \
static ssize_t                              \
lgeusb_ ## field ## _show(struct device *dev, struct device_attribute *attr,   \
		char *buf)                      \
{                                   \
	struct lgeusb_dev *usbdev = _lgeusb_dev; \
	return snprintf(buf, PAGE_SIZE, "%s", usbdev->string);              \
}                                   \
static DEVICE_ATTR(field, S_IRUGO, lgeusb_ ## field ## _show, NULL);

#define LGE_STRING_ATTR(field, buffer)               \
static ssize_t                              \
field ## _show(struct device *dev, struct device_attribute *attr,   \
		char *buf)                      \
{                                   \
	return snprintf(buf, PAGE_SIZE, "%s", buffer);          \
}                                   \
static ssize_t                              \
field ## _store(struct device *dev, struct device_attribute *attr,  \
		const char *buf, size_t size)                   \
{                                   \
	if (size >= sizeof(buffer)) \
		return -EINVAL;         \
	if (sscanf(buf, "%31s", buffer) == 1) {            \
		return size;                        \
	}                               \
	return -ENODEV;                          \
}                                   \
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, field ## _show, field ## _store);

LGE_ID_ATTR(vendor_id, "%04X\n")
LGE_ID_ATTR(factory_pid, "%04X\n")
LGE_ID_ATTR(iSerialNumber, "%d\n")
LGE_RDONLY_STRING_ATTR(product_name, product)
LGE_RDONLY_STRING_ATTR(manufacturer_name, manufacturer)
LGE_RDONLY_STRING_ATTR(fcomposition, fcomposition)
LGE_STRING_ATTR(model_name, model_string)
LGE_STRING_ATTR(sw_version, swver_string)
LGE_STRING_ATTR(sub_version, subver_string)
LGE_STRING_ATTR(phone_id, phoneid_string)

static ssize_t lgeusb_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	int is_factory_cable = 0;
	ssize_t ret = 0;

	if (usbdev->get_factory_cable)
		is_factory_cable = usbdev->get_factory_cable();

	mutex_lock(&lgeusb_lock);
	if (is_factory_cable)
		usbdev->current_mode = LGEUSB_FACTORY_MODE;
	else
		usbdev->current_mode = LGEUSB_ANDROID_MODE;
	mutex_unlock(&lgeusb_lock);
	switch (is_factory_cable) {
	case LGEUSB_FACTORY_56K:
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "factory_56k");
		break;
	case LGEUSB_FACTORY_130K:
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "factory_130k");
		break;
	case LGEUSB_FACTORY_910K:
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "factory_910k");
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "%s\n", "normal");
		break;
	}

	return ret;
}
static DEVICE_ATTR(lge_usb_mode, S_IRUGO, lgeusb_mode_show, NULL);

static struct device_attribute *lge_android_usb_attributes[] = {
	&dev_attr_vendor_id,
	&dev_attr_factory_pid,
	&dev_attr_product_name,
	&dev_attr_manufacturer_name,
	&dev_attr_fcomposition,
	&dev_attr_lge_usb_mode,
	&dev_attr_iSerialNumber,
	&dev_attr_model_name,
	&dev_attr_sw_version,
	&dev_attr_sub_version,
	&dev_attr_phone_id,
	NULL
};

static int lgeusb_create_device_file(struct lgeusb_dev *dev)
{
	struct device_attribute **attrs = lge_android_usb_attributes;
	struct device_attribute *attr;
	int ret;

	while ((attr = *attrs++)) {
		ret = device_create_file(dev->dev, attr);
		if (ret) {
			pr_err("lgeusb: error on creating device file %s\n",
							attr->attr.name);
			return ret;
		}
	}

	return 0;
}

int lgeusb_get_factory_cable(void)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	if (usbdev->get_factory_cable)
		return usbdev->get_factory_cable();
	return 0;
}

int lgeusb_get_vendor_id(void)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	return usbdev ? usbdev->vendor_id : -EINVAL;
}

int lgeusb_get_factory_pid(void)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	return usbdev ? usbdev->factory_pid : -EINVAL;
}

int lgeusb_get_serial_number(void)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	return usbdev ? usbdev->iSerialNumber : -EINVAL;
}

int lgeusb_get_manufacturer_name(char *manufact_name)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	char *manufact = manufact_name;

	if (!manufact || !usbdev || !usbdev->manufacturer)
		return -EINVAL;

	strlcpy(manufact, usbdev->manufacturer, MAX_SERIAL_NO_LEN - 1);
	pr_debug("lgeusb: manfacturer name %s\n", manufact);
	return 0;
}

int lgeusb_get_product_name(char *prod_name)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	char *prod = prod_name;

	if (!prod || !usbdev || !usbdev->product)
		return -EINVAL;

	strlcpy(prod, usbdev->product, MAX_SERIAL_NO_LEN - 1);
	pr_debug("lgeusb: product name %s\n", prod);
	return 0;
}

int lgeusb_get_factory_composition(char *fcomposition)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	char *fcompo = fcomposition;

	if (!fcomposition || !usbdev || !usbdev->fcomposition)
		return -EINVAL;

	strlcpy(fcompo, usbdev->fcomposition, MAX_SERIAL_NO_LEN - 1);
	pr_debug("lgeusb: factory composition %s\n", fcompo);
	return 0;
}

int lgeusb_get_model_name(char *model)
{
	if (!model || strlen(model) > 15)
		return -EINVAL;

	strlcpy(model, model_string, strlen(model) - 1);
	pr_info("lgeusb: model name %s\n", model);
	return 0;
}

int lgeusb_get_phone_id(char *phoneid)
{
	if (!phoneid || strlen(phoneid) > 15)
		return -EINVAL;

	strlcpy(phoneid, phoneid_string, strlen(phoneid) - 1);
	pr_info("lgeusb: phoneid %s\n", phoneid);
	return 0;
}

int lgeusb_get_sw_ver(char *sw_ver)
{
	if (!sw_ver || strlen(sw_ver) > 15)
		return -EINVAL;

	strlcpy(sw_ver, swver_string, strlen(sw_ver) - 1);
	pr_info("lgeusb: sw version %s\n", sw_ver);
	return 0;
}

int lgeusb_get_sub_ver(char *sub_ver)
{
	if (!sub_ver || strlen(sub_ver) > 15)
		return -EINVAL;

	strlcpy(sub_ver, subver_string, strlen(sub_ver) - 1);
	pr_info("lgeusb: sw sub version %s\n", sub_ver);
	return 0;
}

static struct platform_driver lge_android_usb_platform_driver = {
	.driver = {
		.name = "lge_android_usb",
	},
};

#ifdef CONFIG_LGE_USB_G_MULTIPLE_CONFIGURATION
void lgeusb_set_host_os(u16 w_length)
{
	switch (w_length) {
	case MAC_OS_TYPE:
		is_mac_os = true;
		break;
	case WIN_LINUX_TYPE:
		is_mac_os = false;
		break;
	default:
		break;
	}
}

bool lgeusb_get_host_os(void)
{
	return is_mac_os;
}
#endif

static int lgeusb_probe(struct platform_device *pdev)
{
	struct lge_android_usb_platform_data *pdata = pdev->dev.platform_data;
	struct lgeusb_dev *usbdev = _lgeusb_dev;

	dev_dbg(&pdev->dev, "%s: pdata: %p\n", __func__, pdata);

	usbdev->dev = &pdev->dev;

	if (pdata) {
		if (pdata->vendor_id)
			usbdev->vendor_id = pdata->vendor_id;

		if (pdata->factory_pid)
			usbdev->factory_pid = pdata->factory_pid;

		if (pdata->iSerialNumber)
			usbdev->iSerialNumber = pdata->iSerialNumber;

		if (pdata->product_name)
			usbdev->product = pdata->product_name;

		if (pdata->manufacturer_name)
			usbdev->manufacturer = pdata->manufacturer_name;

		if (pdata->factory_composition)
			usbdev->fcomposition = pdata->factory_composition;

		if (pdata->get_factory_cable)
			usbdev->get_factory_cable = pdata->get_factory_cable;
	}

	usbdev->current_mode = LGEUSB_DEFAULT_MODE;

	lgeusb_create_device_file(usbdev);

	return 0;
}

#ifdef CONFIG_LGE_USB_DIAG_LOCK
#define DIAG_DISABLE 0
#define DIAG_ENABLE 1

int user_diag_enable = DIAG_DISABLE;

#ifdef CONFIG_LGE_USB_DIAG_LOCK_SPR
typedef struct {
	int hw_rev;
	char model_name[10];
#ifdef CONFIG_MACH_MSM8996_LUCYE
	char sw_version[64];
#endif
	// LGE_ONE_BINARY ???
	char diag_enable;
} lge_hw_smem_id0_type;

static int lge_diag_get_smem_value(void)
{
	int smem_size = 0;
	lge_hw_smem_id0_type* lge_hw_smem_id0_ptr = (lge_hw_smem_id0_type *)
		(smem_get_entry(SMEM_ID_VENDOR0, &smem_size, 0, 0));
	if (lge_hw_smem_id0_ptr != NULL) {
		pr_info("%s: diag_enable: %d\n", __func__, lge_hw_smem_id0_ptr->diag_enable);
		return lge_hw_smem_id0_ptr->diag_enable;
	} else {
		return 0;
	}
}
#endif

int get_diag_enable(void)
{
#if defined(CONFIG_LGE_USB_FACTORY) && !defined(CONFIG_LGE_USB_DIAG_LOCK_SPR)
	if (lge_get_factory_boot())
		user_diag_enable = DIAG_ENABLE;
#endif

	return user_diag_enable;
}
EXPORT_SYMBOL(get_diag_enable);

static ssize_t read_diag_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret;

	ret = snprintf(buf, PAGE_SIZE, "%d", user_diag_enable);

	return ret;
}
static ssize_t write_diag_enable(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	unsigned char string[2];

	if (sscanf(buf, "%s", string) != 1)
		return -EINVAL;

	if (!strncmp(string, "0", 1))
	{
		user_diag_enable = 0;
	}
	else
	{
		user_diag_enable = 1;
	}

	pr_err("[%s] diag_enable: %d\n", __func__, user_diag_enable);

	return size;
}
static DEVICE_ATTR(diag_enable, S_IRUGO | S_IWUSR,
					read_diag_enable, write_diag_enable);
int lg_diag_create_file(struct platform_device *pdev)
{
	int ret;

	ret = device_create_file(&pdev->dev, &dev_attr_diag_enable);
	if (ret) {
		device_remove_file(&pdev->dev, &dev_attr_diag_enable);
		return ret;
	}
	return ret;
}


int lg_diag_remove_file(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_diag_enable);
	return 0;
}

static int lg_diag_cmd_probe(struct platform_device *pdev)
{
	int ret;
	ret = lg_diag_create_file(pdev);

	return ret;
}

static int lg_diag_cmd_remove(struct platform_device *pdev)
{
	lg_diag_remove_file(pdev);

	return 0;
}

static struct platform_driver lg_diag_cmd_driver = {
	.probe		= lg_diag_cmd_probe,
	.remove		= lg_diag_cmd_remove,
	.driver		= {
		.name = "lg_diag_cmd",
		.owner	= THIS_MODULE,
	},
};
#endif

static int __init lgeusb_init(void)
{
	struct lgeusb_dev *dev;

	pr_info("u_lgeusb init\n");
	mutex_init(&lgeusb_lock);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	_lgeusb_dev = dev;

	/* set default vid, pid and factory id.
	  vid and pid will be overrided. */
	dev->vendor_id = LGE_VENDOR_ID;
	dev->factory_pid = LGE_FACTORY_PID;
#if defined(CONFIG_LGE_USB_DIAG_LOCK) && defined(CONFIG_LGE_FACTORY)
	dev->get_factory_cable = lge_get_factory_boot;
#else
	dev->get_factory_cable = NULL;
#endif

#ifdef CONFIG_LGE_USB_DIAG_LOCK
	platform_driver_register(&lg_diag_cmd_driver);
#endif

#ifdef CONFIG_LGE_USB_DIAG_LOCK_SPR
	user_diag_enable = lge_diag_get_smem_value();
#endif
	return platform_driver_probe(&lge_android_usb_platform_driver,
			lgeusb_probe);
}
module_init(lgeusb_init);

static void __exit lgeusb_cleanup(void)
{
	platform_driver_unregister(&lge_android_usb_platform_driver);
	kfree(_lgeusb_dev);
	_lgeusb_dev = NULL;
}
module_exit(lgeusb_cleanup);
