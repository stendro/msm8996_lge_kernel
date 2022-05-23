/* Copyright (c) 2013-2014, LG Eletronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "[LGCC] %s : " fmt, __func__

#define CONFIG_LGE_PM_USE_BMS
#define CONFIG_LGE_PM_OTP_ENABLE

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/power_supply.h>

#include <soc/qcom/lge/lge_charging_scenario.h>
#include <soc/qcom/lge/lge_cable_detection.h>

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
#include <soc/qcom/lge/lge_pseudo_batt.h>
#endif

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
#include <linux/power/lge_battery_id.h>
#endif

#ifdef CONFIG_LGE_ALICE_FRIENDS
#include <linux/usb.h>
atomic_t in_call_status;
#endif

#define MODULE_NAME "lge_charging_controller"
#define MONITOR_BATTEMP_POLLING_PERIOD  (60 * HZ)
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
#define USB_CURRENT_MAX_MODE_ICL	(900 * 1000)
#define USB_CURRENT_NORMAL_MODE_ICL (500 * 1000)
#define MONITOR_USB_CURRENT_MODE_CHECK_PERIOD  (90 * HZ)
#endif

#ifdef CONFIG_LGE_PM_LLK_MODE
#ifdef CONFIG_MACH_MSM8996_H1_VZW
#define LLK_MAX_THR_SOC 35
#define LLK_MIN_THR_SOC 30
#else
#define LLK_MAX_THR_SOC 50
#define LLK_MIN_THR_SOC 45
#endif
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
#define VZW_SLOW_CHG_CURRENT_MIN 500
#define VZW_SLOW_CHG_RESET_COUNT 5
#define VZW_SLOW_CHG_CHECK_MS	4000
typedef enum vzw_chg_state {
	VZW_NO_CHARGER,
	VZW_NORMAL_CHARGING,
	VZW_INCOMPATIBLE_CHARGING,
	VZW_UNDER_CURRENT_CHARGING,
	VZW_USB_DRIVER_UNINSTALLED,
	VZW_LLK_NOT_CHARGING,
	VZW_CHARGER_STATUS_MAX,
} chg_state;
#endif

enum lgcc_vote_reason {
	LGCC_REASON_DEFAULT,
	LGCC_REASON_LCD,
	LGCC_REASON_CALL,
	LGCC_REASON_TDMB,
	LGCC_REASON_UHD_RECORD,
	LGCC_REASON_MIRACAST,
	LGCC_REASON_MAX,
};

static char *restricted_chg_name[] = {
	[LGCC_REASON_DEFAULT] 		= "DEFAULT",
	[LGCC_REASON_LCD] 		= "LCD",
	[LGCC_REASON_CALL]		= "CALL",
	[LGCC_REASON_TDMB]		= "TDMB",
	[LGCC_REASON_UHD_RECORD]	= "UHDREC",
	[LGCC_REASON_MIRACAST]		= "WFD",
	[LGCC_REASON_MAX]		= NULL,
};

enum {
	RESTRICTED_CHG_STATUS_OFF,
	RESTRICTED_CHG_STATUS_ON,
	RESTRICTED_CHG_STATUS_MODE1,
	RESTRICTED_CHG_STATUS_MODE2,
	RESTRICTED_CHG_STATUS_MAX,
};

static char *retricted_chg_status[] = {
	[RESTRICTED_CHG_STATUS_OFF]	= "OFF",
	[RESTRICTED_CHG_STATUS_ON]	= "ON",
	[RESTRICTED_CHG_STATUS_MODE1]	= "MODE1",
	[RESTRICTED_CHG_STATUS_MODE2]	= "MODE2",
	[RESTRICTED_CHG_STATUS_MAX]	= NULL,
};

extern int lgcc_is_charger_present(void);
extern int lgcc_set_ibat_current(int type, int state, int chg_current);
extern void lgcc_set_tdmb_mode(int on);
extern int lgcc_set_charging_enable(int enable, int state);
extern int lgcc_set_iusb_enable(int enable, int state);
extern int lgcc_get_effective_fcc_result(void);

int lgcc_is_probed = 0;

struct lge_charging_controller{
	struct device			*dev;
	struct power_supply     lgcc_psy;
	struct power_supply		*batt_psy;
	struct power_supply		*usb_psy;
	struct power_supply		*parallel_psy;

#ifdef CONFIG_LGE_PM_USE_BMS
	struct power_supply		*bms_psy;
#endif
#ifdef CONFIG_LGE_USB_TYPE_C
	struct power_supply		*ctype_psy;
#endif

	struct delayed_work battemp_work;
	struct delayed_work step_charging_work;
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	struct delayed_work usb_current_max_work;
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	struct delayed_work slow_chg_work;
#endif
	struct wake_lock lcs_wake_lock;

	enum lge_charging_states battemp_chg_state;

	int chg_current_te;
	int chg_current_max;
	int otp_ibat_current;
	int pseudo_chg_ui;
	int before_battemp;
	int batt_temp;
	int btm_state;
#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	int batt_id_fcc;
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	int usb_current_max_mode;
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	chg_state vzw_chg_mode;
	int is_floated_charger;
	int is_slow_charger;
	int slow_chg_reset_cnt;
	struct work_struct set_vzw_chg_work;
#endif
#if defined(CONFIG_LGE_PM_VZW_REQ) || defined(CONFIG_LGE_PM_LLK_MODE)
	int usb_present;
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	int store_demo_enabled;
	int llk_charging_status;
	int llk_iusb_status;
#endif
	int tdmb_mode_on;
};

enum fcc_voters {
#ifdef CONFIG_MACH_MSM8996_H1
	DEFAULT_FCC_VOTER,
#endif
	ESR_PULSE_FCC_VOTER,
	BATT_TYPE_FCC_VOTER,
	RESTRICTED_CHG_FCC_VOTER,
	OTP_FCC_VOTER,
	THERMAL_FCC_VOTER,
	STEP_FCC_VOTER,
	USER_FCC_VOTER,
	NUM_FCC_VOTER,
};

#ifdef CONFIG_LGE_PM_LLK_MODE
enum enable_voters {
	USER_EN_VOTER,
	POWER_SUPPLY_EN_VOTER,
	USB_EN_VOTER,
	WIRELESS_EN_VOTER,
	THERMAL_EN_VOTER,
	OTG_EN_VOTER,
	WEAK_CHARGER_EN_VOTER,
	FAKE_BATTERY_EN_VOTER,
	NUM_EN_VOTERS,
};
#endif

enum battchg_enable_voters {
	/* userspace has disabled battery charging */
	BATTCHG_USER_EN_VOTER,
#ifndef CONFIG_BATTERY_MAX17050
	/* battery charging disabled while loading battery profiles */
	BATTCHG_UNKNOWN_BATTERY_EN_VOTER,
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	BATTCHG_LLK_MODE_EN_VOTER,
#endif
	NUM_BATTCHG_EN_VOTERS,
};

static struct lge_charging_controller *the_controller;

static int is_hvdcp_present(void)
{
	union power_supply_propval ret = {0, };

	the_controller->usb_psy->get_property(the_controller->usb_psy,
			POWER_SUPPLY_PROP_REAL_TYPE, &ret);

	pr_err("%s - charger_type[%d]\n", __func__, ret.intval);

	if (ret.intval == POWER_SUPPLY_TYPE_USB_HVDCP ||
		ret.intval == POWER_SUPPLY_TYPE_USB_HVDCP_3)
		return true;

#ifdef CONFIG_LGE_USB_TYPE_C
#ifdef CONFIG_LGE_PM_VZW_REQ
	the_controller->usb_psy->get_property(the_controller->usb_psy,
			POWER_SUPPLY_PROP_FASTCHG, &ret);

	pr_err("%s - fastchg_type[%d]\n", __func__, ret.intval);

	if (ret.intval == 1)
		return true;
#endif
#endif

	return false;
}

#define STEP_CHRG_THR_1ST			4300
#define STEP_CHRG_THR_2ND			4400
#define STEP_FCC_1ST				2500
#define STEP_FCC_2ND				1800
#define STEP_FCC_3RD				1800
#define STEP_CHARGING_CHECK_TIME                (30 * HZ)
#define USB_C_INPUT_CURRENT_MAX                 3000
static void step_charging_check_work(struct work_struct *work)
{
	union power_supply_propval pval = {0, };
	int vbat_mv, prev_fcc_ma;
#ifdef CONFIG_LGE_USB_TYPE_C
	int c_type, c_mA, rc;

	if (!the_controller->ctype_psy)
		the_controller->ctype_psy = power_supply_get_by_name("usb_pd");

	if (the_controller->ctype_psy) {
		rc = the_controller->ctype_psy->get_property(the_controller->ctype_psy,
				POWER_SUPPLY_PROP_TYPE, &pval);
		c_type = pval.intval;

		if (c_type == POWER_SUPPLY_TYPE_CTYPE) {
			rc = the_controller->ctype_psy->get_property(the_controller->ctype_psy,
					POWER_SUPPLY_PROP_CURRENT_MAX, &pval);
			c_mA = pval.intval / 1000;
			pr_err("%s - POWER_SUPPLY_TYPE_CTYPE - c_mA[%d]\n",
					__func__, c_mA);
		}
	}

	if (!lgcc_is_probed) {
		pr_err("lgcc is not probed yet.\n");
		return;
	}

	if (is_hvdcp_present() || c_mA == USB_C_INPUT_CURRENT_MAX) {
#else
	if (!lgcc_is_probed) {
		pr_err("lgcc is not probed yet..\n");
		return;
	}

	if (is_hvdcp_present()) {
#endif
		the_controller->batt_psy->get_property(the_controller->batt_psy,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
		vbat_mv = pval.intval / 1000;

		prev_fcc_ma = lgcc_get_effective_fcc_result();
		pr_err("VBAT = %d prev_fcc_ma = %d\n", vbat_mv, prev_fcc_ma);

		if (vbat_mv < STEP_CHRG_THR_1ST) {
			if (prev_fcc_ma != STEP_FCC_1ST) {
				pr_err("change FCC to %dmA\n", STEP_FCC_1ST);
				lgcc_set_ibat_current(BATT_TYPE_FCC_VOTER, true, STEP_FCC_1ST);
				lgcc_set_ibat_current(STEP_FCC_VOTER, true, STEP_FCC_1ST);
			}
		} else if ((vbat_mv >= STEP_CHRG_THR_1ST) &&
				(vbat_mv < STEP_CHRG_THR_2ND)) {
			if (prev_fcc_ma != STEP_FCC_2ND) {
				pr_err("change FCC to %dmA\n", STEP_FCC_2ND);
				lgcc_set_ibat_current(STEP_FCC_VOTER, true, STEP_FCC_2ND);
			}
		} else if (vbat_mv >= STEP_CHRG_THR_2ND) {
			if (prev_fcc_ma != STEP_FCC_3RD) {
				pr_err("change FCC to %dmA\n", STEP_FCC_3RD);
				lgcc_set_ibat_current(STEP_FCC_VOTER, true, STEP_FCC_3RD);
			}
		}
		schedule_delayed_work(&the_controller->step_charging_work,
			STEP_CHARGING_CHECK_TIME);
	}
#ifdef CONFIG_LGE_PM_VZW_REQ
	power_supply_changed(&the_controller->lgcc_psy);
#endif
}

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
static int lgcc_get_batt_id(void)
{
	struct power_supply *batt_id_psy;
	union power_supply_propval prop = {0,};
	int batt_id;

	if (lge_battery_check()) {
		batt_id_psy = power_supply_get_by_name("battery_id");
		if (batt_id_psy) {
			batt_id_psy->get_property(batt_id_psy,
				POWER_SUPPLY_PROP_BATTERY_ID, &prop);
			batt_id = prop.intval;
		} else {
			pr_err("batt_id psy is not initialized\n");
			return -EINVAL;
		}
	} else {
		pr_err("battery id is invalid\n");
		return -EINVAL;
	}
	return batt_id;
}
#endif

#ifdef CONFIG_LGE_PM_VZW_REQ
static void lgcc_set_vzw_chg_work(struct work_struct *work)
{
	struct lge_charging_controller *controller = container_of(work,
			            struct lge_charging_controller,
						set_vzw_chg_work);
	union power_supply_propval pval = {0, };
	bool usb_present = false;
	int rc;

	rc = controller->usb_psy->get_property(controller->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
	usb_present = pval.intval;

	if (!usb_present) {
		controller->vzw_chg_mode = VZW_NO_CHARGER;
		pr_info("no charger is detected\n");
	} else {
		if (controller->is_floated_charger) {
			controller->vzw_chg_mode = VZW_INCOMPATIBLE_CHARGING;
			power_supply_set_current_limit(controller->usb_psy,
					USB_CURRENT_NORMAL_MODE_ICL);
			pr_info("floated charger detected\n");
		} else if (controller->is_slow_charger) {
			controller->vzw_chg_mode = VZW_UNDER_CURRENT_CHARGING;
			pr_info("slow charger is detected\n");
		} else {
			controller->vzw_chg_mode = VZW_NORMAL_CHARGING;
			pr_info("normal charger is detected\n");
		}
#ifdef CONFIG_LGE_PM_LLK_MODE
		if (controller->store_demo_enabled) {
			pr_err("llk_iusb_status = %d, llk_charging_status = %d\n",
					controller->llk_iusb_status,
					controller->llk_charging_status);
			if (!controller->llk_charging_status) {
				controller->vzw_chg_mode = VZW_LLK_NOT_CHARGING;
				pr_info("charging stopped in llk mode\n");
			} else {
				controller->vzw_chg_mode = VZW_NORMAL_CHARGING;
				pr_info("normal charger is detected\n");
			}
		}
#endif
	}
	power_supply_changed(&controller->lgcc_psy);
}
#endif

#ifdef CONFIG_LGE_PM_LLK_MODE
static bool lgcc_check_llk_mode(int usb_present)
{
	int capacity = 0;
	int battery_charging_enabled;
	int iusb_enabled;
	struct power_supply     *bms_psy;
	union power_supply_propval pval = {0, };
	int rc = 0;

	bms_psy = power_supply_get_by_name("bms");

	if (!bms_psy) {
		pr_err("bms psy is not initialized\n");
		return false;
	}

	bms_psy->get_property(bms_psy,
			POWER_SUPPLY_PROP_FIRST_SOC_EST_DONE, &pval);
	if (pval.intval == false) {
		pr_err("first soc is not estimated\n");
		return false;
	}

	rc = the_controller->batt_psy->get_property(the_controller->batt_psy,
			POWER_SUPPLY_PROP_CAPACITY, &pval);
	capacity = pval.intval;

	rc = the_controller->batt_psy->get_property(the_controller->batt_psy,
			POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &pval);
	battery_charging_enabled = pval.intval;

	rc = the_controller->batt_psy->get_property(the_controller->batt_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &pval);
	iusb_enabled = pval.intval;

	if (usb_present) {
		if (capacity > LLK_MAX_THR_SOC) {
			if (iusb_enabled == true) {
				lgcc_set_iusb_enable(0, USER_EN_VOTER);
				pr_info("disable iusb by LLK mode. soc[%d]\n", capacity);
				the_controller->llk_iusb_status = false;
			}
		} else {
			if (iusb_enabled == false) {
				lgcc_set_iusb_enable(1, USER_EN_VOTER);
				pr_info("enable iusb by LLK mode. soc[%d]\n", capacity);
			}
			the_controller->llk_iusb_status = true;
		}

		if (capacity >= LLK_MAX_THR_SOC) {
			if (battery_charging_enabled == true) {
				lgcc_set_charging_enable(0, BATTCHG_LLK_MODE_EN_VOTER);
				pr_info("stop charging by LLK mode. soc[%d]\n", capacity);
			}
			the_controller->llk_charging_status = false;
		} else if ((capacity >= LLK_MIN_THR_SOC) &&
				(capacity <= LLK_MAX_THR_SOC)) {
			if (battery_charging_enabled == false)
				the_controller->llk_charging_status = false;
			else {
				lgcc_set_charging_enable(0, BATTCHG_LLK_MODE_EN_VOTER);
				pr_info("stop charging by LLK mode. soc[%d]\n", capacity);
				the_controller->llk_charging_status = true;
			}
		} else if (capacity < LLK_MIN_THR_SOC) {
			if (battery_charging_enabled == false) {
				lgcc_set_charging_enable(1, BATTCHG_LLK_MODE_EN_VOTER);
				pr_info("resume charging by LLK mode. soc[%d]\n", capacity);
			}
			the_controller->llk_charging_status = true;
		}
	}
	return true;
}
#endif

static char *lgcc_supplied_from[] = {
	    "battery",
};

static enum power_supply_property lgcc_cc_properties[] = {
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	POWER_SUPPLY_PROP_USB_CURRENT_MAX_MODE,
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	POWER_SUPPLY_PROP_VZW_CHG,
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	POWER_SUPPLY_PROP_STORE_DEMO_ENABLED,
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	POWER_SUPPLY_PROP_PSEUDO_BATT,
#endif
	POWER_SUPPLY_PROP_TDMB_MODE_ON,
};

static int lgcc_get_property(struct power_supply *psy,
		    enum power_supply_property prop,
			union power_supply_propval *val)
{
	struct lge_charging_controller *controller = container_of(psy,
			struct lge_charging_controller, lgcc_psy);


	switch (prop) {
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX_MODE:
		val->intval = controller->usb_current_max_mode;
		pr_err("usb_current_max_mode = %d\n", controller->usb_current_max_mode);
		break;
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	case POWER_SUPPLY_PROP_VZW_CHG:
		val->intval = controller->vzw_chg_mode;
		break;
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		val->intval = controller->store_demo_enabled;
		break;
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	case POWER_SUPPLY_PROP_PSEUDO_BATT:
		val->intval = get_pseudo_batt_info(PSEUDO_BATT_MODE);
		break;
#endif
	case POWER_SUPPLY_PROP_TDMB_MODE_ON:
		val->intval = controller->tdmb_mode_on;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define RESTRICTED_CHG_CURRENT     500
#define NORMAL_CHG_CURRENT_MAX     3100
static int lgcc_set_property(struct power_supply *psy,
		    enum power_supply_property prop,
			const union power_supply_propval *val)
{
	int rc = 0;
	struct lge_charging_controller *controller = container_of(psy,
			struct lge_charging_controller, lgcc_psy);

	switch (prop) {
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX_MODE:
		controller->usb_current_max_mode = val->intval;
		pr_err("usb_current_max_mode = %d\n", controller->usb_current_max_mode);
		break;
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		controller->store_demo_enabled = val->intval;
		pr_info("llk mode is set to [%d]\n", controller->store_demo_enabled);
		break;
#endif
	case POWER_SUPPLY_PROP_TDMB_MODE_ON:
		controller->tdmb_mode_on = val->intval;
		pr_info("tdmb mode is set to [%d]\n", controller->tdmb_mode_on);

		if (controller->tdmb_mode_on == 1) {
			lgcc_set_tdmb_mode(1);
			lgcc_set_ibat_current(USER_FCC_VOTER,
					true, RESTRICTED_CHG_CURRENT);
		} else if (controller->tdmb_mode_on == 0) {
			lgcc_set_tdmb_mode(0);
			lgcc_set_ibat_current(USER_FCC_VOTER,
					true, NORMAL_CHG_CURRENT_MAX);
		}
		break;
	default:
		return -EINVAL;
	}

	return rc;
}
static int lgcc_property_is_writerable(struct power_supply *psy,
		                        enum power_supply_property prop)
{
	int rc;

	switch(prop) {
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX_MODE:
		rc = 1;
		break;
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		rc = 1;
		break;
#endif
	case POWER_SUPPLY_PROP_TDMB_MODE_ON:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static void lgcc_external_power_changed(struct power_supply *psy)
{
	struct lge_charging_controller *controller = container_of(psy,
			struct lge_charging_controller, lgcc_psy);
	int rc = 0;
	union power_supply_propval pval = {0, };
	int usb_present = 0;
#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	int batt_id;
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	bool is_need_to_update = false;
	bool is_floated_charger = false;
#endif

	if (!controller->usb_psy)
		controller->usb_psy = power_supply_get_by_name("usb");

	if (controller->usb_psy) {
		rc = controller->usb_psy->get_property(controller->usb_psy,
				POWER_SUPPLY_PROP_PRESENT, &pval);
		usb_present = pval.intval;
	}

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	if (usb_present) {
		batt_id = lgcc_get_batt_id();

		if (batt_id > 0) {
#ifdef CONFIG_MACH_MSM8996_H1
			if (batt_id == BATT_ID_DS2704_L || batt_id == BATT_ID_DS2704_C ||
				batt_id == BATT_ID_ISL6296_L || batt_id == BATT_ID_ISL6296_C) {
				if (controller->batt_id_fcc != 1600) {
					lgcc_set_ibat_current(USER_FCC_VOTER, true, 1600);
					controller->batt_id_fcc = 1600;
					pr_info("1600mA battery is detected set fcc to 1600mA\n");
				}
			}
#endif
		} else {
			pr_err("battery id is not vaild\n");
		}
	} else {
		controller->batt_id_fcc = 0;
	}
#endif

#ifdef CONFIG_LGE_PM_VZW_REQ
	if (controller->usb_present != usb_present) {
		controller->usb_present = usb_present;
		if (controller->usb_present) {
			/* check slow charging after VZW_SLOW_CHG_CHECK_MS */
			if (lgcc_is_probed)
				schedule_delayed_work(&controller->slow_chg_work,
						msecs_to_jiffies(VZW_SLOW_CHG_CHECK_MS));
		}
		else {
			controller->is_floated_charger = false;
			controller->is_slow_charger = false;
#ifdef CONFIG_LGE_PM_LLK_MODE
			controller->llk_charging_status = false;
#endif
		}
		is_need_to_update = true;
	}

	/* check floated charger */
	is_floated_charger = controller->usb_psy->is_floated_charger;
	if (controller->is_floated_charger != is_floated_charger) {
		pr_info("floated charger[%d] is detected\n", is_floated_charger);
		controller->is_floated_charger = is_floated_charger;
		is_need_to_update = true;
	}
#ifdef CONFIG_LGE_PM_LLK_MODE
	if (controller->store_demo_enabled)
		is_need_to_update = lgcc_check_llk_mode(usb_present);
#endif

	if (is_need_to_update) {
		pr_info("vzw_chg_mode[%d] aicl_ma[%d] present[%d]\n",
			controller->vzw_chg_mode, pval.intval, controller->usb_present);
		if (!work_pending(&controller->set_vzw_chg_work))
				schedule_work(&controller->set_vzw_chg_work);
	}
#else
#ifdef CONFIG_LGE_PM_LLK_MODE
	if (controller->usb_present != usb_present) {
		controller->usb_present = usb_present;
		if (!controller->usb_present) {
			controller->llk_charging_status = false;
			controller->llk_iusb_status = false;
		}
	}

	if (controller->store_demo_enabled) {
		pr_err("lgcc_check_llk_mode iusb : %d, charging : %d\n",
				controller->llk_iusb_status,
				controller->llk_charging_status);
		lgcc_check_llk_mode(usb_present);
	}
#endif
#endif
	return;
}

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
static void usb_current_max_check_work(struct work_struct *work)
{
	union power_supply_propval pval = {0, };
	enum power_supply_type usb_supply_type;
	int current_icl;

	the_controller->usb_psy->get_property(the_controller->usb_psy,
			POWER_SUPPLY_PROP_APSD_RERUN_NEED, &pval);

	if (lge_is_factory_cable() || pval.intval == 1) {
		pr_err("%s : skip current_max_check work\n", __func__);
		return;
	}

	the_controller->usb_psy->get_property(the_controller->usb_psy,
			POWER_SUPPLY_PROP_REAL_TYPE, &pval);

	usb_supply_type = pval.intval;

	the_controller->usb_psy->get_property(the_controller->usb_psy,
			POWER_SUPPLY_PROP_CURRENT_MAX, &pval);
	current_icl = pval.intval;

	if (usb_supply_type == POWER_SUPPLY_TYPE_USB) {
		pr_err("usb_current_max_mode = %d\n",
				the_controller->usb_current_max_mode);

		if (the_controller->usb_current_max_mode == 1) {
			if (current_icl != USB_CURRENT_MAX_MODE_ICL) {
				power_supply_set_current_limit(the_controller->usb_psy,
						USB_CURRENT_MAX_MODE_ICL);
				pr_err("set usb max to 900mA\n");
			}
		}
	}
	schedule_delayed_work(&the_controller->usb_current_max_work,
			MONITOR_USB_CURRENT_MODE_CHECK_PERIOD);
}
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
static void lgcc_slow_chg_work(struct work_struct *work)
{
	union power_supply_propval pval = {0, };
	int aicl_done = false;
	int aicl_ma = 0;
	int usb_present;

	the_controller->usb_psy->get_property(the_controller->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &pval);
	usb_present = pval.intval;

	if(!usb_present) {
		the_controller->is_slow_charger = false;
		pr_err("usb is unplugged\n");
		return;
	}

	if (!lgcc_is_probed) {
		pr_err("lgcc is not probed yet...\n");
		return;
	}

	if (is_hvdcp_present()) {
		pr_err("hvdcp is present. it is not slow charger\n");
		the_controller->is_slow_charger = false;
		return;
	}

	the_controller->batt_psy->get_property(the_controller->batt_psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED, &pval);
	aicl_done = pval.intval;

	aicl_ma = the_controller->batt_psy->get_property(the_controller->batt_psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, &pval);
	aicl_ma = pval.intval / 1000;

	pr_err("aicl ma = %d\n", aicl_ma);

	if (aicl_done == true && aicl_ma < VZW_SLOW_CHG_CURRENT_MIN) {
		the_controller->is_slow_charger = true;
		the_controller->slow_chg_reset_cnt = 0;
		pr_err("slow chg icl %dmA\n", aicl_ma);
		if (!work_pending(&the_controller->set_vzw_chg_work))
			schedule_work(&the_controller->set_vzw_chg_work);
	}
	return;
}
#endif

#define DCP_CHG_MAX_IBAT     1800
#define HVDCP_CHG_MAX_IBAT   3100
static int lgcc_ibat_limit = 0;
static int lgcc_set_ibat_limit(const char *val,
		struct kernel_param *kp)
{
	int ret;

	if (!lgcc_is_probed) {
		pr_err("lgcc is not probed yet....\n");
		return 0;
	}

	if (is_hvdcp_present()) {
		pr_info("hvdcp is present.. skip this settings\n");
		return 0;
	}

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	} else if (!the_controller) {
		pr_err("lgcc is not ready\n");
		return 0;
	} else if (lgcc_ibat_limit < 0) {
		pr_err("invalid value setting\n");
		return 0;
	}

	if (lgcc_ibat_limit == DCP_CHG_MAX_IBAT ||
			lgcc_ibat_limit == HVDCP_CHG_MAX_IBAT) {
		pr_err("set lgcc_ibat_limit to DEFAULT_FCC(3100mA)\n");
		lgcc_ibat_limit = HVDCP_CHG_MAX_IBAT;
		lgcc_set_ibat_current(THERMAL_FCC_VOTER, true, lgcc_ibat_limit);
	}
	else {
		pr_err("set lgcc_ibat_limit to %d\n", lgcc_ibat_limit);
		lgcc_set_ibat_current(THERMAL_FCC_VOTER, true, lgcc_ibat_limit);
	}

	return 0;
}
module_param_call(lgcc_ibat_limit, lgcc_set_ibat_limit,
	param_get_int, &lgcc_ibat_limit, 0644);

static int lgcc_ibat_hvdcp_limit = 0;
static int lgcc_set_ibat_hvdcp_limit(const char *val,
        struct kernel_param *kp)
{
	int ret;

	if (!lgcc_is_probed) {
		pr_err("lgcc is not probed yet.....\n");
		return 0;
	}

	if (!is_hvdcp_present()) {
		pr_info("hvdcp is not present.... skip this settings\n");
		return 0;
	}

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	} else if (!the_controller) {
		pr_err("lgcc is not ready\n");
		return 0;
	} else if (lgcc_ibat_hvdcp_limit < 0) {
		pr_err("invalid value setting\n");
		return 0;
	}

	if (lgcc_ibat_hvdcp_limit == DCP_CHG_MAX_IBAT ||
			lgcc_ibat_hvdcp_limit == HVDCP_CHG_MAX_IBAT) {
		lgcc_ibat_hvdcp_limit = HVDCP_CHG_MAX_IBAT;
		pr_err("set lgcc_ibat_hvdcp_limit to DEFAULT_FCC(3000mA)\n");
		lgcc_set_ibat_current(THERMAL_FCC_VOTER,
			true, lgcc_ibat_hvdcp_limit);
	}
	else {
		pr_err("set lgcc_ibat_hvdcp_limit to %d\n",
			lgcc_ibat_hvdcp_limit);
		lgcc_set_ibat_current(THERMAL_FCC_VOTER,
			true, lgcc_ibat_hvdcp_limit);
	}

	return 0;
}
module_param_call(lgcc_ibat_hvdcp_limit, lgcc_set_ibat_hvdcp_limit,
	param_get_int, &lgcc_ibat_hvdcp_limit, 0644);

static int lgcc_iusb_control = 0;
static int lgcc_set_iusb_control(const char *val,
		struct kernel_param *kp){
	int ret;
	union power_supply_propval pval = {0,};

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	} else if (!the_controller) {
		pr_err("lgcc is not ready\n");
		return 0;
	} else if (lgcc_ibat_limit < 0) {
		pr_err("invalid value setting\n");
		return 0;
	}

	pr_err("set lgcc_iusb_control to %d\n", lgcc_iusb_control);

	pval.intval = lgcc_iusb_control;
	the_controller->batt_psy->set_property(the_controller->batt_psy,
		POWER_SUPPLY_PROP_CHARGING_ENABLED, &pval);

	return 0;
}
module_param_call(lgcc_iusb_control, lgcc_set_iusb_control,
	param_get_int, &lgcc_iusb_control, 0644);

static char *restricted_charging;
static int restricted_charging_param_set(const char *, const struct kernel_param *);
static struct kernel_param_ops restricted_charging_param_ops = {
	.set 	=	restricted_charging_param_set,
	.get 	=	param_get_charp,
	.free	=	param_free_charp,
};

module_param_cb(restricted_charging, &restricted_charging_param_ops,
		&restricted_charging, 0644);

static int restricted_charging_set_current(int reason, int status)
{
	int chg_curr;

	switch (reason) {
		case LGCC_REASON_CALL:
			if (status == RESTRICTED_CHG_STATUS_ON) {
				chg_curr = RESTRICTED_CHG_CURRENT;
				lgcc_set_ibat_current(RESTRICTED_CHG_FCC_VOTER,true,chg_curr);
#ifdef CONFIG_LGE_ALICE_FRIENDS
				atomic_set(&in_call_status, 1);
#endif
			} else {
				chg_curr = NORMAL_CHG_CURRENT_MAX;
				lgcc_set_ibat_current(RESTRICTED_CHG_FCC_VOTER,true,chg_curr);
#ifdef CONFIG_LGE_ALICE_FRIENDS
				atomic_set(&in_call_status, 0);
#endif
			}
			break;
		case LGCC_REASON_TDMB:
			if (status == RESTRICTED_CHG_STATUS_MODE1 ||
					status == RESTRICTED_CHG_STATUS_MODE2) {
				lgcc_set_tdmb_mode(1);
				chg_curr = RESTRICTED_CHG_CURRENT;
			} else {
				lgcc_set_tdmb_mode(0);
				chg_curr = NORMAL_CHG_CURRENT_MAX;
			}
			lgcc_set_ibat_current(USER_FCC_VOTER,true, chg_curr);
			break;
		case LGCC_REASON_LCD:
		case LGCC_REASON_UHD_RECORD:
		case LGCC_REASON_MIRACAST:
			chg_curr = -EINVAL;
			break;
		case LGCC_REASON_DEFAULT:
		default:
			chg_curr = NORMAL_CHG_CURRENT_MAX;
			lgcc_set_ibat_current(USER_FCC_VOTER,true, chg_curr);
			lgcc_set_ibat_current(USER_FCC_VOTER,true, chg_curr);
			break;
	}

	return chg_curr;
}

static int restricted_charging_find_name(char *name)
{
	int i;

	for (i = 0; i < LGCC_REASON_MAX; i++) {
		// exclude voters which is not registered in the user space.
		if (restricted_chg_name[i] == NULL)
			continue;

		if (!strcmp(name, restricted_chg_name[i]))
			return i;
	}

	return -EINVAL;
}

static int restricted_charging_find_status(char *status)
{
	int i;

	for (i = 0; i < RESTRICTED_CHG_STATUS_MAX; i++) {

		if (!strcmp(status, retricted_chg_status[i]))
			return i;
	}

	return -EINVAL;
}

static int restricted_charging_param_set(const char *val, const struct kernel_param *kp)
{
	char *s = strstrip((char *)val);
	char *voter_name, *voter_status;
	int chg_curr, name, status, ret;

	if (s == NULL) {
		pr_err("Restrict charging param is NULL! \n");
		return -EINVAL;
	}

	pr_info("Restricted charging param = %s \n", s);

	ret = param_set_charp(val, kp);

	if (ret) {
		pr_err("Error setting param %d\n", ret);
		return ret;
	}

	voter_name = strsep(&s, " ");
	voter_status = s;

	name = restricted_charging_find_name(voter_name);
	status = restricted_charging_find_status(voter_status);

	if (name == -EINVAL || status == -EINVAL) {
		pr_err("Restrict charging param is invalid! \n");
		return -EINVAL;
	}

	chg_curr = restricted_charging_set_current(name, status);
	pr_info("voter_name = %s[%d], voter_status = %s[%d], chg_curr = %d \n",
				voter_name, name, voter_status, status, chg_curr);


	return 0;
}

static int lgcc_thermal_mitigation = 0;
static int lgcc_set_thermal_chg_current(const char *val,
		struct kernel_param *kp){

	int ret;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_err("error setting value %d\n", ret);
		return ret;
	}

	if (!the_controller) {
		pr_err("lgcc is not ready\n");
		return 0;
	}

	if (lgcc_thermal_mitigation <= 0)
		the_controller->chg_current_te = the_controller->chg_current_max;
	else
		the_controller->chg_current_te = lgcc_thermal_mitigation;

	pr_err("lgcc_thermal_mitigation = %d, chg_current_te_te = %d\n",
			lgcc_thermal_mitigation, the_controller->chg_current_te);

	cancel_delayed_work_sync(&the_controller->battemp_work);
	schedule_delayed_work(&the_controller->battemp_work, HZ*1);

	return 0;
}
module_param_call(lgcc_thermal_mitigation, lgcc_set_thermal_chg_current,
	param_get_int, &lgcc_thermal_mitigation, 0644);


#define OTP_CHG_NORMAL_IBAT     3100
#define OTP_CHG_DECCUR_IBAT     450
#define OTP_CHG_STOP_IBAT       0
static void lge_monitor_batt_temp_work(struct work_struct *work){

	struct charging_info req;
	struct charging_rsp res;
	bool is_changed = false;
	union power_supply_propval ret = {0,};
	int pmi_chg_current_max = 0;

	the_controller->batt_psy->get_property(the_controller->batt_psy,
		POWER_SUPPLY_PROP_TEMP, &ret);
	req.batt_temp = ret.intval;
	the_controller->batt_temp = req.batt_temp;

	the_controller->batt_psy->get_property(the_controller->batt_psy,
		POWER_SUPPLY_PROP_VOLTAGE_NOW, &ret);
	req.batt_volt = ret.intval;

	the_controller->batt_psy->get_property(the_controller->batt_psy,
		POWER_SUPPLY_PROP_CURRENT_NOW, &ret);
	req.current_now = ret.intval / 1000;

	the_controller->batt_psy->get_property(the_controller->batt_psy,
		POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &ret);
	pmi_chg_current_max = ret.intval / 1000;

	the_controller->batt_psy->get_property(the_controller->parallel_psy,
		POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &ret);
	the_controller->chg_current_max =
		pmi_chg_current_max + ret.intval / 1000;

	req.chg_current_ma = the_controller->chg_current_max;

	if (the_controller->chg_current_te != -EINVAL)
		req.chg_current_te = the_controller->chg_current_te;
	else
		req.chg_current_te = the_controller->chg_current_max;

	pr_debug("chg_current_max = %d\n", the_controller->chg_current_max);
	pr_debug("chg_curren_te = %d\n", the_controller->chg_current_te);

	req.is_charger = lgcc_is_charger_present();

	lge_monitor_batt_temp(req, &res);

	if (((res.change_lvl != STS_CHE_NONE) && req.is_charger) ||
			(res.force_update == true)) {
		if (res.change_lvl == STS_CHE_NORMAL_TO_DECCUR ||
				(res.state == CHG_BATT_DECCUR_STATE &&
				 res.chg_current != DC_CURRENT_DEF &&
				 res.change_lvl != STS_CHE_STPCHG_TO_DECCUR)) {
			pr_info("ibatmax_set STS_CHE_NORMAL_TO_DECCUR\n");
			the_controller->otp_ibat_current = OTP_CHG_DECCUR_IBAT;
			lgcc_set_ibat_current(OTP_FCC_VOTER, true,
				the_controller->otp_ibat_current);
			lgcc_set_charging_enable(1, BATTCHG_USER_EN_VOTER);

		} else if (res.change_lvl == STS_CHE_NORMAL_TO_STPCHG ||
				(res.state == CHG_BATT_STPCHG_STATE)) {
			pr_info("ibatmax_set STS_CHE_NORMAL_TO_STPCHG. "
					"holding lcs wake_lock\n");
			wake_lock(&the_controller->lcs_wake_lock);
			the_controller->otp_ibat_current = OTP_CHG_STOP_IBAT;
			lgcc_set_ibat_current(OTP_FCC_VOTER, true,
				the_controller->otp_ibat_current);
			lgcc_set_charging_enable(0, BATTCHG_USER_EN_VOTER);

		} else if (res.change_lvl == STS_CHE_DECCUR_TO_NORMAL) {
			pr_info("ibatmax_set STS_CHE_DECCUR_TO_NORMAL\n");
			the_controller->otp_ibat_current = OTP_CHG_NORMAL_IBAT;
			lgcc_set_ibat_current(OTP_FCC_VOTER, true,
				the_controller->otp_ibat_current);
			lgcc_set_charging_enable(1, BATTCHG_USER_EN_VOTER);

		} else if (res.change_lvl == STS_CHE_DECCUR_TO_STPCHG) {
			pr_info("ibatmax_set STS_CHE_DECCUR_TO_STPCHG. "
					"holding lcs wake_lock\n");
			wake_lock(&the_controller->lcs_wake_lock);
			the_controller->otp_ibat_current = OTP_CHG_STOP_IBAT;
			lgcc_set_ibat_current(OTP_FCC_VOTER, true,
				the_controller->otp_ibat_current);
			lgcc_set_charging_enable(0, BATTCHG_USER_EN_VOTER);

		} else if (res.change_lvl == STS_CHE_STPCHG_TO_NORMAL) {
			pr_info("ibatmax_set STS_CHE_STPCHG_TO_NORMAL. "
					"releasing lcs wake_lock\n");
			wake_unlock(&the_controller->lcs_wake_lock);
			the_controller->otp_ibat_current = OTP_CHG_NORMAL_IBAT;
			lgcc_set_ibat_current(OTP_FCC_VOTER, true,
				the_controller->otp_ibat_current);
			lgcc_set_charging_enable(1, BATTCHG_USER_EN_VOTER);

		} else if (res.change_lvl == STS_CHE_STPCHG_TO_DECCUR) {
			pr_info("ibatmax_set STS_CHE_STPCHG_TO_DECCUR. "
					"releasing lcs wake_lock\n");
			wake_unlock(&the_controller->lcs_wake_lock);
			the_controller->otp_ibat_current = OTP_CHG_DECCUR_IBAT;
			lgcc_set_ibat_current(OTP_FCC_VOTER, true,
				the_controller->otp_ibat_current);
			lgcc_set_charging_enable(1, BATTCHG_USER_EN_VOTER);

		} else if (res.force_update == true &&
				res.state == CHG_BATT_NORMAL_STATE &&
				res.chg_current != DC_CURRENT_DEF) {
			pr_info("ibatmax_set CHG_BATT_NORMAL_STATE\n");
			lgcc_set_charging_enable(1, BATTCHG_USER_EN_VOTER);
		}
	}

	pr_err("otp_ibat_current=%d\n", the_controller->otp_ibat_current);

	pr_debug("the_controller->pseudo_chg_ui = %d, res.pseudo_chg_ui = %d\n",
			the_controller->pseudo_chg_ui, res.pseudo_chg_ui);

	if (the_controller->pseudo_chg_ui ^ res.pseudo_chg_ui) {
		is_changed = true;
		the_controller->pseudo_chg_ui = res.pseudo_chg_ui;
	}

	pr_debug("the_controller->btm_state = %d, res.btm_state = %d\n",
			the_controller->btm_state, res.btm_state);
	if (the_controller->btm_state ^ res.btm_state) {
		is_changed = true;
		the_controller->btm_state = res.btm_state;
	}

	if (the_controller->before_battemp != req.batt_temp) {
		is_changed = true;
		the_controller->before_battemp = req.batt_temp;
	}

	if (is_changed == true)
		power_supply_changed(the_controller->batt_psy);

#ifdef CONFIG_LGE_PM_USE_BMS
	if (!the_controller->bms_psy)
		the_controller->bms_psy = power_supply_get_by_name("bms");

	if (the_controller->bms_psy) {
		the_controller->bms_psy->get_property(the_controller->bms_psy,
				POWER_SUPPLY_PROP_CAPACITY, &ret);

		pr_info("Reported Capacity : %d / voltage : %d\n",
				ret.intval, req.batt_volt/1000);
	}
#endif

	schedule_delayed_work(&the_controller->battemp_work,
			MONITOR_BATTEMP_POLLING_PERIOD);
}

int get_pseudo_ui(void){

	if( !(the_controller == NULL) ){
		return the_controller->pseudo_chg_ui;
	}
	return 0;
}

int get_btm_state(void){

	if( !(the_controller == NULL) ){
		return the_controller->btm_state;
	}
	return 0;
}

void start_lgcc_work(int mdelay){
	int delay = mdelay / 1000;
#ifdef CONFIG_LGE_PM_OTP_ENABLE
	pr_err("start otp work\n");
	schedule_delayed_work(&the_controller->battemp_work,
		(delay * HZ));
#endif

	pr_err("start step_charging work\n");
	schedule_delayed_work(&the_controller->step_charging_work,
		(delay * HZ));

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	if (!lge_is_factory_cable())
		schedule_delayed_work(&the_controller->usb_current_max_work,
				(delay * HZ));
#endif
}
EXPORT_SYMBOL(start_lgcc_work);

void stop_lgcc_work(void) {
#ifdef CONFIG_LGE_PM_OTP_ENABLE
	pr_err("stop otp work\n");
	cancel_delayed_work(&the_controller->battemp_work);
#endif
	if (wake_lock_active(&the_controller->lcs_wake_lock)) {
		pr_err("releasing lcs_wake_lock\n");
		wake_unlock(&the_controller->lcs_wake_lock);
	}

	pr_err("stop_step_charging_work~!!\n");
	cancel_delayed_work(&the_controller->step_charging_work);
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	if (!lge_is_factory_cable())
		cancel_delayed_work(&the_controller->usb_current_max_work);
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	cancel_delayed_work(&the_controller->slow_chg_work);
#endif
}
EXPORT_SYMBOL(stop_lgcc_work);

static int lge_charging_controller_probe(struct platform_device *pdev)
{
	struct lge_charging_controller *controller;
	union power_supply_propval pval = {0, };
	int ret;

	controller = kzalloc(sizeof(struct lge_charging_controller),
		GFP_KERNEL);

	if(!controller){
		pr_err("lge_charging_controller memory allocation failed.\n");
		return -ENOMEM;
	}

	the_controller = controller;
	controller->dev = &pdev->dev;

	controller->lgcc_psy.name = "lgcc";
	controller->lgcc_psy.properties = lgcc_cc_properties;
	controller->lgcc_psy.num_properties = ARRAY_SIZE(lgcc_cc_properties);
	controller->lgcc_psy.get_property = lgcc_get_property;
	controller->lgcc_psy.set_property = lgcc_set_property;
	controller->lgcc_psy.supplied_from = lgcc_supplied_from;
	controller->lgcc_psy.num_supplies = ARRAY_SIZE(lgcc_supplied_from);
	controller->lgcc_psy.property_is_writeable = lgcc_property_is_writerable;
	controller->lgcc_psy.external_power_changed = lgcc_external_power_changed;

	ret = power_supply_register(controller->dev, &controller->lgcc_psy);

	if (ret < 0) {
		pr_err("power_supply_register charger controller failed ret=%d\n", ret);
		goto unregister_lgcc;
	}

	controller->usb_psy = power_supply_get_by_name("usb");

	if(!controller->usb_psy){
		pr_err("usb power_supply not found deferring probe\n");
		ret = -EPROBE_DEFER;
		goto unregister_lgcc;
	}

	controller->batt_psy = power_supply_get_by_name("battery");

	if(!controller->batt_psy){
		pr_err("battery power_supply not found deferring probe\n");
		ret = -EPROBE_DEFER;
		goto unregister_lgcc;
	}

	controller->parallel_psy = power_supply_get_by_name("usb-parallel");
	if(!controller->parallel_psy){
		pr_err("parallel power_supply not found deferring probe\n");
		ret = -EPROBE_DEFER;
		goto unregister_lgcc;
	}

#ifdef CONFIG_LGE_PM_USE_BMS
	controller->bms_psy = power_supply_get_by_name("bms");

	if(!controller->bms_psy){
		pr_err("bms power_supply is not ready\n");
		controller->bms_psy = NULL;
	}
#endif
	wake_lock_init(&controller->lcs_wake_lock,
			WAKE_LOCK_SUSPEND, "lge_charging_scenario");

	INIT_DELAYED_WORK(&controller->battemp_work,
			lge_monitor_batt_temp_work);
	INIT_DELAYED_WORK(&controller->step_charging_work,
		step_charging_check_work);
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	INIT_DELAYED_WORK(&controller->usb_current_max_work,
			usb_current_max_check_work);
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	INIT_WORK(&controller->set_vzw_chg_work, lgcc_set_vzw_chg_work);
	INIT_DELAYED_WORK(&controller->slow_chg_work, lgcc_slow_chg_work);
	controller->slow_chg_reset_cnt = 0;
#endif

	controller->chg_current_max = -EINVAL;
	controller->chg_current_te = controller->chg_current_max;

	controller->otp_ibat_current = 0;

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX_MODE
	the_controller->batt_psy->get_property(the_controller->batt_psy,
			POWER_SUPPLY_PROP_USB_CURRENT_MAX_MODE, &pval);
	controller->usb_current_max_mode = pval.intval;
	pr_err("usb_current_max_mode = %d\n", controller->usb_current_max_mode);
#endif

	lgcc_is_probed = 1;

	pr_info("LG Charging controller probe done~!!\n");

	return 0;

unregister_lgcc:
	power_supply_unregister(&controller->lgcc_psy);
	kfree(the_controller);
	return ret;

}

static int lge_charging_controller_remove(struct platform_device *pdev)
{
	wake_lock_destroy(&the_controller->lcs_wake_lock);
	kfree(the_controller);
	return 0;
}

static struct platform_driver lge_charging_controller_driver = {
	.probe = lge_charging_controller_probe,
	.remove = lge_charging_controller_remove,
	.driver = {
		.name = MODULE_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init lge_charging_controller_init(void)
{
	return platform_driver_register(&lge_charging_controller_driver);
}

static void lge_charging_controller_exit(void)
{
	platform_driver_unregister(&lge_charging_controller_driver);
}

static struct platform_device lge_charging_controller_platform_device = {
	.name   = "lge_charging_controller",
	.id = 0,
};

static int __init lge_charging_controller_device_init(void)
{
	pr_info("%s st\n", __func__);
	return platform_device_register(&lge_charging_controller_platform_device);
}

static void lge_charging_controller_device_exit(void)
{
	platform_device_unregister(&lge_charging_controller_platform_device);
}

late_initcall(lge_charging_controller_init);
module_exit(lge_charging_controller_exit);
late_initcall(lge_charging_controller_device_init);
module_exit(lge_charging_controller_device_exit);
MODULE_DESCRIPTION("LGE Charging Controller driver");
MODULE_LICENSE("GPL v2");
