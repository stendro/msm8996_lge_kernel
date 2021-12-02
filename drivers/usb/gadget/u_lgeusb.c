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
#include <linux/slab.h>
#include <linux/platform_device.h>

#define LGE_VENDOR_ID 0x1004
#define LGE_PRODUCT_ID 0x618E
#define LGE_FACTORY_PID 0x6000

struct lgeusb_dev {
	struct device *dev;
	u16 vendor_id;
	u16 product_id;
	u16 factory_pid;
};

static struct lgeusb_dev *_lgeusb_dev;

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
static DEVICE_ATTR(field, 0644, lgeusb_ ## field ## _show, \
		lgeusb_ ## field ## _store);

LGE_ID_ATTR(vendor_id, "%04X\n")
LGE_ID_ATTR(product_id, "%04X\n")
LGE_ID_ATTR(factory_pid, "%04X\n")

static ssize_t lgeusb_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	ret = snprintf(buf, PAGE_SIZE, "%s\n", "normal");
	return ret;
}
static DEVICE_ATTR(lge_usb_mode, 0444, lgeusb_mode_show, NULL);

static struct device_attribute *lge_android_usb_attributes[] = {
	&dev_attr_vendor_id,
	&dev_attr_product_id,
	&dev_attr_factory_pid,
	&dev_attr_lge_usb_mode,
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

static struct platform_driver lge_android_usb_platform_driver = {
	.driver = {
		.name = "lge_android_usb",
	},
};

static int lgeusb_probe(struct platform_device *pdev)
{
	struct lgeusb_dev *usbdev = _lgeusb_dev;
	usbdev->dev = &pdev->dev;

	lgeusb_create_device_file(usbdev);

	return 0;
}

static int __init lgeusb_init(void)
{
	struct lgeusb_dev *dev;

	pr_info("u_lgeusb init\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	_lgeusb_dev = dev;

	/* set default vid, pid and factory id.
	  vid and pid will be overrided. */
	dev->vendor_id = LGE_VENDOR_ID;
	dev->product_id = LGE_PRODUCT_ID;
	dev->factory_pid = LGE_FACTORY_PID;

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
