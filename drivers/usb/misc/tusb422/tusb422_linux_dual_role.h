/*
 * TUSB422 Power Delivery
 *
 * Author: Brian Quach <brian.quach@ti.com>
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _TUSB422_LINUX_DUAL_ROLE_H_
#define _TUSB422_LINUX_DUAL_ROLE_H_

#ifdef CONFIG_DUAL_ROLE_USB_INTF

#include <linux/device.h>
#include <linux/usb/class-dual-role.h>

extern struct dual_role_phy_instance *tusb422_dual_role_phy;

int tusb422_linux_dual_role_init(struct device *dev);

#endif /* CONFIG_DUAL_ROLE_USB_INTF */
#endif /* _TUSB422_LINUX_DUAL_ROLE_H_ */
