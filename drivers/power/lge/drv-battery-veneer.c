/*
 * LGE Power class
 *
 * Copyright (C) 2016 LG Electronics, Inc
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "BATTERY-VENEER: %s: " fmt, __func__
#define pr_veneer(fmt, ...) pr_info(fmt, ##__VA_ARGS__)

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/wakelock.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>

#include "inc-limit-voter.h"
#include "inc-unified-sysfs.h"
#include "inc-charging-time.h"
#include "inc-battery-veneer.h"

#define	EXTERNAL_CHANGED_PRESENT_USB		BIT(1)
#define	EXTERNAL_CHANGED_PRESENT_WIRELESS	BIT(2)
#define	EXTERNAL_CHANGED_PRESENT_BATTERY	BIT(3)

#define BATTERY_VENEER_COMPATIBLE	"lge,battery-veneer"
#define BATTERY_VENEER_DRIVER		"lge-battery-veneer"
#define BATTERY_VENEER_NAME		"battery-veneer"

#define BATTERY_VENEER_WAKELOCK 	BATTERY_VENEER_NAME": charging"
#define BATTERY_VENEER_NOTREADY		INT_MAX

static enum charger_type detect_charger_type(bool presence_usb, bool presence_dc);
static int detect_charger_power(enum charger_type type);

struct battery_veneer {
/* module descripters */
	struct device* veneer_dev;
	struct power_supply veneer_psy;
#ifdef CONFIG_MACH_MSM8996_LUCYE
	struct power_supply* wlc_psy;
#endif
	struct wake_lock veneer_wakelock;

/* shadow states */
	// present or not
	bool presence_usb;
	bool presence_dc;
	// battery status
	enum battery_health_psy battery_health;
	int battery_temperature;
	int battery_uvoltage;
	int battery_soc;

/* limited charging/inputing current values by LGE scenario (unit in mA). */
	int limited_iusb;
	int limited_ibat;
	int limited_idc;

/* for charging time */
	enum charger_type chgtime_charger; // { NIL, SDP, CDP, DCP, QC2, QC3, PD2, PD3, EVP, WLC }
	struct delayed_work chgtime_dwork;
};

static int limit_input_current(const union power_supply_propval *propval) {
	struct power_supply* psy_batt = power_supply_get_by_name("battery");

	if (psy_batt && psy_batt->set_property) {
		pr_veneer("bypass '%d' to psy battery\n", propval->intval);
		return psy_batt->set_property(psy_batt,
				POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, propval);
	} else {
		pr_veneer("psy battery is not ready\n");
		return -ENXIO;
	}
}

static void chgtime_dwork_schedule(struct battery_veneer* veneer_me) {
	int delay = (veneer_me->chgtime_charger == NIL || veneer_me->chgtime_charger == QC3)
			? 0 : 3500;

	if (delayed_work_pending(&veneer_me->chgtime_dwork))
		cancel_delayed_work(&veneer_me->chgtime_dwork);
	schedule_delayed_work(&veneer_me->chgtime_dwork, msecs_to_jiffies(delay));
}

static void chgtime_dwork_main(struct work_struct *work) {
	struct power_supply* psy_batt = power_supply_get_by_name("battery");
	struct battery_veneer* veneer_me = container_of(to_delayed_work(work),
		struct battery_veneer, chgtime_dwork);

	enum charger_type	charger = veneer_me->chgtime_charger;
	int 			power = detect_charger_power(charger);

	static int 		sample = INT_MAX;
	static int 		counter = 0;

	if (charger == DCP) {
		pr_veneer("getting power (%d) : %d\n", counter, power);
		if (sample != power) {
			++counter;
			sample = power;
			chgtime_dwork_schedule(veneer_me);
			return;
		}
	}

	sample = INT_MAX;
	counter = 0;

	if (charger && power) {
		charging_time_initiate(charger, power);
	}
	else {
		charging_time_clear();
	}

	if (psy_batt) {
		pr_veneer("Update uevent!\n");
		power_supply_changed(psy_batt);
	}
}

static bool charging_wakelock_acquire(struct battery_veneer* veneer_me) {
	if (!wake_lock_active(&veneer_me->veneer_wakelock)) {
		pr_veneer("%s", BATTERY_VENEER_WAKELOCK);
		wake_lock(&veneer_me->veneer_wakelock);

		return true;
	}
	return false;
}

static bool charging_wakelock_release(struct battery_veneer* veneer_me) {
	if (wake_lock_active(&veneer_me->veneer_wakelock)) {
		pr_veneer("%s", BATTERY_VENEER_WAKELOCK);
		wake_unlock(&veneer_me->veneer_wakelock);

		return true;
	}
	return false;
}

static void charging_wakelock_control(struct battery_veneer* veneer_me,
	bool input_present, bool charge_done, int soc) {

	if (input_present) {
		if (charge_done) {
			charging_wakelock_release(veneer_me);
		}
		else if (soc < 100) {
			charging_wakelock_acquire(veneer_me);
		}
		else {
			; /* Do noting */
		}
	}
	else
		charging_wakelock_release(veneer_me);
}

static enum charger_type detect_charger_type(bool presence_usb,
	bool presence_dc) {
	enum charger_type type = NIL;

	/* This function is designed to detect detailed charger type from common psy interface.
	 * But there are two usb psys to support code-compatibility of the wired chargers,
	 * so I have to search the type in 'usb_pd' one more at this time.
	 * Please fix the such irrational dependency on your turn.
	 */
	if (presence_usb) {
		union power_supply_propval psp;
		struct power_supply* psy_usb = power_supply_get_by_name("usb");

		if (psy_usb && psy_usb->get_property &&
			!psy_usb->get_property(psy_usb, POWER_SUPPLY_PROP_REAL_TYPE, &psp)) {

			switch (psp.intval) {
			case POWER_SUPPLY_TYPE_USB :
				type = SDP;
				break;
			case POWER_SUPPLY_TYPE_USB_CDP :
				type = CDP;
				break;
			case POWER_SUPPLY_TYPE_USB_DCP :
				type = DCP;
				break;
			case POWER_SUPPLY_TYPE_USB_HVDCP :
			case POWER_SUPPLY_TYPE_USB_HVDCP_3 :
				type = QC3;
				break;
			default : /* impossible */
				type = NIL;
				break;
			}

			/* For PD, do detect one more time */
			psp.intval = 0;
			psy_usb = power_supply_get_by_name("usb_pd");
			if (psy_usb
				&& psy_usb->get_property
				&& !psy_usb->get_property(psy_usb, POWER_SUPPLY_PROP_TYPE, &psp)
				&& psp.intval == POWER_SUPPLY_TYPE_CTYPE_PD) {
				type = PD3;
			}
			else {
				; /* Do nothing : Entering here would be normal for other models. */
			}
		}
		else {
			pr_veneer("Error on retriving psy_usb property\n");
		}
	}
	else if (presence_dc) {
		type = WLC;
	}
	else {
		; /* Do nothing */
	}

	return type;
}

static int detect_charger_voltage(enum charger_type type) {
	switch (type) {
		case QC2 :
		case QC3 :
		case PD2 :
		case PD3 :
		case EVP :
			return 9;
		case DCP :
		case SDP :
		case CDP :
		case WLC :
			return 5;
		case NIL :
		default :
			return 0;
	}
}

static int detect_charger_power(enum charger_type type) {
	struct power_supply* psy_parallel = power_supply_get_by_name("usb-parallel");
	struct power_supply* psy_batt = power_supply_get_by_name("battery");
	int max_volatge = detect_charger_voltage(type);
	int max_current = 0;

	if (type == WLC) {
		max_current = 900;
	}
	else if (type != NIL) {
		/* get AICL value*/

		union power_supply_propval psp = { .intval = 0 };

		// getting parallel ICL
		if (psy_parallel && psy_parallel->get_property
			&& !psy_parallel->get_property(psy_parallel, POWER_SUPPLY_PROP_STATUS, &psp)) {
			if (psp.intval == POWER_SUPPLY_STATUS_CHARGING) {
				if (!psy_parallel->get_property(psy_parallel, POWER_SUPPLY_PROP_CURRENT_MAX, &psp)) {
					pr_veneer("ICL for parallel is %d\n", psp.intval);
					max_current = psp.intval;
				}
			}
		}
		// getting main ICL
		if (psy_batt && psy_batt->get_property
			&& !psy_batt->get_property(psy_batt, POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &psp)) {
			max_current += psp.intval;
		}

		// round(max_current) to 100mA scale
		max_current = (max_current/1000 + 50) / 100 * 100;
	}
	else {
		max_current = 0;
	}

	return max_current * max_volatge;
}

static char* psy_external_suppliers [] = { "battery", "bms", "ac", "usb",
		"usb-parallel", "usb-pd", "usb_pd", "dc", "dc-wireless", };
static void psy_external_changed(struct power_supply *external_supplier);
static const char* psy_property_name(enum power_supply_property prop);
static int psy_property_set(struct power_supply *psy,
		enum power_supply_property prop, const union power_supply_propval *val);
static int psy_property_get(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val);
static int psy_property_writeable(struct power_supply *psy,
		enum power_supply_property prop);
static enum power_supply_property psy_property_list [] = {
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
#ifdef CONFIG_MACH_MSM8996_LUCYE
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CONNECTION_TYPE,
#endif
};

static const char* psy_property_name(enum power_supply_property prop) {
	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return "POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT";
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		return "POWER_SUPPLY_PROP_TIME_TO_FULL_NOW";
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return "POWER_SUPPLY_PROP_VOLTAGE_NOW";
	case POWER_SUPPLY_PROP_TEMP:
		return "POWER_SUPPLY_PROP_TEMP";
	default:
		return "NOT_SUPPORTED_PROP";
	}
}

static int psy_property_set(struct power_supply *psy_me,
		enum power_supply_property prop,
		const union power_supply_propval *val) {

	int rc = 0;
	struct battery_veneer *veneer_me = container_of(psy_me,
			struct battery_veneer, veneer_psy);

	pr_veneer("setting property %s\n", psy_property_name(prop));

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT: {
		rc = limit_input_current(val);
		if (!rc) {
			int limited_current = vote_current(val);

			switch (vote_type(val)) {
			case LIMIT_VOTER_IUSB:
				veneer_me->limited_iusb = limited_current;
				break;
			case LIMIT_VOTER_IBAT:
				veneer_me->limited_ibat = limited_current;
				break;
			case LIMIT_VOTER_IDC:
				veneer_me->limited_idc = limited_current;
				break;
			default:
				rc = -EINVAL;
			}
		}
	}
	break;

	default:
		rc = -EINVAL;
	}

	return rc;
}

#ifdef CONFIG_MACH_MSM8996_LUCYE
static void get_property_from_wlc(struct battery_veneer *veneer_me,
				enum power_supply_property property,
				union power_supply_propval *prop)
{
	int rc;

	if (!veneer_me->wlc_psy) {
		veneer_me->wlc_psy = power_supply_get_by_name("dc-wireless");
		if (!veneer_me->wlc_psy) {
			prop->intval = 0;
			return;
		}
	}

	rc = veneer_me->wlc_psy->get_property(veneer_me->wlc_psy, property, prop);
	if (rc)
		pr_veneer("wireless psy doesn't support reading prop %d rc = %d\n",
			property, rc);
}
#endif

static int psy_property_get(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val) {

	int rc = 0;
	struct battery_veneer *veneer_me = container_of(psy,
			struct battery_veneer, veneer_psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		// When someone wants to read POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
		// it should set limit_type to 'val->intval' prior to calling get_property()
		switch (val->intval) {
		case LIMIT_VOTER_IUSB:
			if (BATTERY_VENEER_NOTREADY != veneer_me->limited_iusb)
				val->intval += veneer_me->limited_iusb;
			else
				rc = -EAGAIN;
			break;
		case LIMIT_VOTER_IBAT:
			if (BATTERY_VENEER_NOTREADY != veneer_me->limited_ibat)
				val->intval += veneer_me->limited_ibat;
			else
				rc = -EAGAIN;
			break;
		case LIMIT_VOTER_IDC:
			if (BATTERY_VENEER_NOTREADY != veneer_me->limited_idc)
				val->intval += veneer_me->limited_idc;
			else
				rc = -EAGAIN;
			break;
		default:
			// In the case of accessing without protocol.
			// But do not return -EINVAL to avoid client's confusing.
			val->intval = -1;
		}
		break;

	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW : {
		bool ready = false;
		struct power_supply* psy_fg = power_supply_get_by_name("bms");

		/* Is SoC ready? */
		if (psy_fg && psy_fg->get_property &&
			!psy_fg->get_property(psy_fg, POWER_SUPPLY_PROP_SOC_REPORTING_READY, val))
			ready = val->intval;

		/* get time to full only on charging */
		val->intval = (ready && (veneer_me->presence_usb || veneer_me->presence_dc)) ?
			charging_time_remains(veneer_me->battery_soc) :
			-1;

	}	break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW :
		if (BATTERY_VENEER_NOTREADY != veneer_me->battery_uvoltage)
			val->intval = veneer_me->battery_uvoltage;
		else
			rc = -EAGAIN;
		break;

	case POWER_SUPPLY_PROP_TEMP :
		if (BATTERY_VENEER_NOTREADY != veneer_me->battery_temperature)
			val->intval = veneer_me->battery_temperature;
		else
			rc = -EAGAIN;
		break;
#ifdef CONFIG_MACH_MSM8996_LUCYE
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_CONNECTION_TYPE:
		get_property_from_wlc(veneer_me,prop,val);
		break;
#endif
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int psy_property_writeable(struct power_supply *psy,
		enum power_supply_property prop) {
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		rc = 1;
		break;

	default:
		rc = 0;
	}

	return rc;
}

static void psy_external_changed(struct power_supply *psy_me) {

	union power_supply_propval buffer;
	struct battery_veneer *veneer_me = container_of(psy_me,
		struct battery_veneer, veneer_psy);

	struct power_supply* psy_batt = power_supply_get_by_name("battery");
	struct power_supply* psy_usb = power_supply_get_by_name("usb");
	struct power_supply* psy_dc = power_supply_get_by_name("dc");

	/* Update form usb */
	if (psy_usb && psy_usb->get_property &&
		!psy_usb->get_property(psy_usb, POWER_SUPPLY_PROP_PRESENT, &buffer)) {
		/* Update usb present */
		veneer_me->presence_usb = !!buffer.intval;
	}
	else {
		pr_veneer("psy usb is not ready\n");
		veneer_me->presence_usb = false;
	}

	/* Update from dc */
	if (psy_dc && psy_dc->get_property &&
		!psy_dc->get_property(psy_dc, POWER_SUPPLY_PROP_PRESENT, &buffer)) {
		/* Update dc present */
		veneer_me->presence_dc = !!buffer.intval;
	}
	else {
		pr_veneer("psy dc is not ready\n");
		veneer_me->presence_dc = false;
	}

	/* Update from battery */
	if (psy_batt && psy_batt->get_property) {
		enum charger_type current_type = detect_charger_type(veneer_me->presence_usb, veneer_me->presence_dc);

		/* Update Temperature/Voltage/SoC */
		if (!psy_batt->get_property(psy_batt, POWER_SUPPLY_PROP_TEMP, &buffer)) {
			veneer_me->battery_temperature = buffer.intval;
		}
		if (!psy_batt->get_property(psy_batt, POWER_SUPPLY_PROP_VOLTAGE_NOW, &buffer)) {
			veneer_me->battery_uvoltage = buffer.intval;
		}
		if (!psy_batt->get_property(psy_batt, POWER_SUPPLY_PROP_CAPACITY, &buffer)) {
			veneer_me->battery_soc = buffer.intval;
		}

		/* Update wake lock */
		if (!psy_batt->get_property(psy_batt, POWER_SUPPLY_PROP_CHARGE_DONE, &buffer)) {
			bool input_present = veneer_me->presence_usb || veneer_me->presence_dc;
			bool charge_done = !!buffer.intval;
			charging_wakelock_control(veneer_me,
				input_present, charge_done, veneer_me->battery_soc);
		}

		/* Initiate charging time */
		if (veneer_me->chgtime_charger != current_type) {
			veneer_me->chgtime_charger = current_type;
			chgtime_dwork_schedule(veneer_me);
		}
	}
	else {
		pr_veneer("psy battery is not ready\n");
	}
}

static int battery_veneer_devicetree(struct device_node *dev_node,
		struct battery_veneer *veneer_me) {

	int ret = 0;

	ret |= unified_sysfs_build(dev_node);

	return ret;
}

static int battery_veneer_psy(struct device *parent, struct power_supply *psy) {
	int ret = 0;

	psy->name = BATTERY_VENEER_NAME;
	psy->type = POWER_SUPPLY_TYPE_BATTERY;
	psy->get_property = psy_property_get;
	psy->set_property = psy_property_set;
	psy->properties = psy_property_list;
	psy->property_is_writeable = psy_property_writeable;
	psy->num_properties = ARRAY_SIZE(psy_property_list);
	psy->supplied_from = psy_external_suppliers;
	psy->num_supplies = ARRAY_SIZE(psy_external_suppliers);
	psy->external_power_changed = psy_external_changed;

	ret = power_supply_register(parent, psy);
	if (ret < 0) {
		dev_err(parent, "Unable to register wlc_psy ret = %d\n", ret);
	}

	return ret;
}

static int battery_veneer_probe(struct platform_device *pdev) {
	int ret = -EPROBE_DEFER;
	struct battery_veneer *veneer_me;
	struct device_node *dev_node = pdev->dev.of_node;

	pr_veneer("Start\n");

	veneer_me = kzalloc(sizeof(struct battery_veneer), GFP_KERNEL);
	if (veneer_me) {
		veneer_me->veneer_dev = &pdev->dev;

	/* Initialize veneer by default */
		// presence will be updated properly when valid psys are registered.
		veneer_me->presence_usb = false;
		veneer_me->presence_dc = false;
		// below shadows can be read before being initialized.
		veneer_me->battery_health = BATTERY_HEALTH_UNKNOWN;
		veneer_me->battery_temperature = BATTERY_VENEER_NOTREADY;
		veneer_me->battery_uvoltage = BATTERY_VENEER_NOTREADY;
		veneer_me->battery_soc = BATTERY_VENEER_NOTREADY;
		veneer_me->limited_iusb = BATTERY_VENEER_NOTREADY;
		veneer_me->limited_ibat = BATTERY_VENEER_NOTREADY;
		veneer_me->limited_idc = BATTERY_VENEER_NOTREADY;
	}
	else {
		pr_err("Failed to alloc memory\n");
		goto fail;
	}

	if (dev_node) {
		ret = battery_veneer_devicetree(dev_node, veneer_me);
		if (ret < 0) {
			pr_err("Fail to parse devicetree\n");
			goto fail;
		}
	}

	ret = battery_veneer_psy(veneer_me->veneer_dev, &veneer_me->veneer_psy);
	if (ret < 0) {
		pr_err("Fail to register psy\n");
		goto fail;
	}

	wake_lock_init(&veneer_me->veneer_wakelock,
			WAKE_LOCK_SUSPEND, BATTERY_VENEER_WAKELOCK);

	INIT_DELAYED_WORK(&veneer_me->chgtime_dwork,
			chgtime_dwork_main);

	platform_set_drvdata(pdev, veneer_me);

	ret = 0;
	goto success;

fail:
	kfree(veneer_me);
	pr_veneer("End (%s)\n", ret==0 ? "success" : "fail");
success:
	return ret;
}

static int battery_veneer_remove(struct platform_device *pdev) {
	struct battery_veneer *veneer_me = platform_get_drvdata(pdev);

	power_supply_unregister(&veneer_me->veneer_psy);
	platform_set_drvdata(pdev, NULL);
	kfree(veneer_me);
	return 0;
}

static const struct of_device_id battery_veneer_match [] = {
	{ .compatible = BATTERY_VENEER_COMPATIBLE },
	{ },
};

static const struct platform_device_id battery_veneer_id [] = {
	{ BATTERY_VENEER_DRIVER, 0 },
	{ },
};

static struct platform_driver battery_veneer_driver = {
	.driver = {
		.name = BATTERY_VENEER_DRIVER,
		.owner = THIS_MODULE,
		.of_match_table = battery_veneer_match,
	},
	.probe = battery_veneer_probe,
	.remove = battery_veneer_remove,
	.id_table = battery_veneer_id,
};

static int __init battery_veneer_init(void) {
	return platform_driver_register(&battery_veneer_driver);
}

static void __exit battery_veneer_exit(void) {
	platform_driver_unregister(&battery_veneer_driver);
}

module_init(battery_veneer_init);
module_exit(battery_veneer_exit);

MODULE_DESCRIPTION(BATTERY_VENEER_DRIVER);
MODULE_LICENSE("GPL v2");
