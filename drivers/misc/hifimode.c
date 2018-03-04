/*
 * Author: stendro
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * Possible values for "enable_hifi_mode" are :
 *
 *   0 - HPH Mode is Class-H Low Power (default)
 *   1 - HPH Mode is Class-H HiFi
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/hifimode.h>
#include <linux/string.h>

int enable_hifi_mode = 0;

static int __init get_hifi_opt(char *ehm)
{
	if (strcmp(ehm, "0") == 0) {
		enable_hifi_mode = 0;
	} else if (strcmp(ehm, "1") == 0) {
		enable_hifi_mode = 1;
	} else {
		enable_hifi_mode = 0;
	}
	return 1;
}

__setup("ehm=", get_hifi_opt);

static ssize_t enable_hifi_mode_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	size_t count = 0;
	count += sprintf(buf, "%d\n", enable_hifi_mode);
	return count;
}

static ssize_t enable_hifi_mode_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%d ", &enable_hifi_mode);
	if (enable_hifi_mode < 0 || enable_hifi_mode > 1)
		enable_hifi_mode = 0;

	return count;
}

static struct kobj_attribute enable_hifi_mode_attribute =
__ATTR(enable_hifi_mode, 0664, enable_hifi_mode_show, enable_hifi_mode_store);

static struct attribute *enable_hifi_mode_attrs[] = {
&enable_hifi_mode_attribute.attr, NULL,
};

static struct attribute_group enable_hifi_mode_attr_group = {
.attrs = enable_hifi_mode_attrs,
};

/* Initialize hifi mode sysfs folder */
static struct kobject *enable_hifi_mode_kobj;

int enable_hifi_mode_init(void)
{
	int enable_hifi_mode_retval;

	enable_hifi_mode_kobj = kobject_create_and_add("hifi_mode", kernel_kobj);
	if (!enable_hifi_mode_kobj) {
			return -ENOMEM;
	}

	enable_hifi_mode_retval = sysfs_create_group(enable_hifi_mode_kobj, &enable_hifi_mode_attr_group);

	if (enable_hifi_mode_retval)
		kobject_put(enable_hifi_mode_kobj);

	if (enable_hifi_mode_retval)
		kobject_put(enable_hifi_mode_kobj);

	return (enable_hifi_mode_retval);
}

void enable_hifi_mode_exit(void)
{
	kobject_put(enable_hifi_mode_kobj);
}

module_init(enable_hifi_mode_init);
module_exit(enable_hifi_mode_exit);
