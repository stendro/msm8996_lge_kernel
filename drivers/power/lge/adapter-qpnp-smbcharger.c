/*
 * CAUTION! :
 * 	This file will be included at the end of "qpnp-smbcharger.c".
 * 	So "qpnp-smbcharger.c" should be touched before you start to build.
 * 	If not, your work will not be applied to the built image
 * 	because the build system doesn't care the update time of this file.
 */

#include "inc-limit-voter.h"

static int current_control_iusb(struct smbchg_chip* chip, int current_ma) {
	return 0;
}

static int current_control_ibat(struct smbchg_chip* chip, int current_ma) {
	int rc = 0;

	if (current_ma != LIMIT_TOTALLY_BLOCKED) {
		if (current_ma == LIMIT_TOTALLY_RELEASED) {
			rc |= vote(chip->fcc_votable, BATTERY_VENEER_FCC_VOTER,
				false, 0);
		}
		else {
			rc |= vote(chip->fcc_votable, BATTERY_VENEER_FCC_VOTER,
				true, current_ma);
		}

		rc |= vote(chip->battchg_suspend_votable,
			BATTERY_VENEER_EN_VOTER, false, 0);
	}
	else {
		pr_info("Stop charging\n");
		rc |= vote(chip->battchg_suspend_votable,
			BATTERY_VENEER_EN_VOTER, true, 0);
	}

	if (rc)
		rc = -EINVAL;

	return rc;
}

static int current_control_idc(struct smbchg_chip* chip, int current_ma) {
	return 0;
}

/* static bool is_fg_ready(struct smbchg_chip *chip) {
	* Check whether the data of fg system, eg. bms, is valid or not.
	*
	* In the case of lucye, we have no choice but to do it by reading "POWER_SUPPLY_PROP_FIRST_SOC_EST_DONE".
	* In other words, reading fg data(temp, voltage and so on...) before POWER_SUPPLY_PROP_FIRST_SOC_EST_DONE
	* are not guaranted values.
	* But in your case of using other battery driver or #define feature,
	* Please DO consider the best approach for it on your system.
	* One of 'The best approach' is registering psy-'fg' to psy AFTER setting data is finished.
	* Then the psy-'battery' can return an error code on get_property rather than returning unstable data.
	*
	* The only one point I recommend to you in this comment is that when you do porting,
	* DO NOT use POWER_SUPPLY_PROP_FIRST_SOC_EST_DONE without such consideration.
	*
	int ready;
	int rc = get_property_from_fg(chip, POWER_SUPPLY_PROP_FIRST_SOC_EST_DONE, &ready);
	if (rc || !ready) {
		pr_smb(PR_STATUS, "BMS(Gauging system) is not ready yet.\n");
		return false;
	}
	else
		return true;
} */

///////////////////////////////////////////////////////////////////////////////

#define PROPERTY_CONSUMED_WITH_SUCCESS	0
#define PROPERTY_CONSUMED_WITH_FAIL	EINVAL
#define PROPERTY_BYPASS_REASON_NOENTRY	ENOENT
#define PROPERTY_BYPASS_REASON_ONEMORE	EAGAIN

static enum power_supply_property smbchg_battery_properties_append [] = {
		POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
		POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
		POWER_SUPPLY_PROP_CHARGE_DONE, };

static enum power_supply_property _smbchg_battery_properties_ext[ARRAY_SIZE(
		smbchg_battery_properties)
		+ ARRAY_SIZE(smbchg_battery_properties_append)] = { 0, };

static int smbchg_battery_get_property_pre(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smbchg_chip *chip = container_of(psy,
		struct smbchg_chip, batt_psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		/* Do nothing and just consume getting */
		break;

	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW: {
		struct power_supply* veneer_psy = power_supply_get_by_name("battery-veneer");
		if (!veneer_psy || !veneer_psy->get_property ||
			veneer_psy->get_property(veneer_psy, POWER_SUPPLY_PROP_TIME_TO_FULL_NOW, val))
			rc = PROPERTY_CONSUMED_WITH_FAIL;
	}	break;

	case POWER_SUPPLY_PROP_CHARGE_DONE:
		val->intval = (get_prop_batt_status(chip) == POWER_SUPPLY_STATUS_FULL);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_TEMP:
		rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;
	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
		break;
	}

	return rc;
}

static int smbchg_battery_get_property_post(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val,
		int rc) {
	return rc;
}

static int smbchg_battery_set_property_pre(struct power_supply *psy,
		enum power_supply_property prop, const union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smbchg_chip *chip = container_of(psy,
		struct smbchg_chip, batt_psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT: {
		enum limit_type type = vote_type(val);
		int limit = vote_current(val); // in mA

		switch (type) {
		case LIMIT_VOTER_IUSB:
			rc = current_control_iusb(chip, limit);
			break;
		case LIMIT_VOTER_IBAT:
			rc = current_control_ibat(chip, limit);
			break;
		case LIMIT_VOTER_IDC:
			rc = current_control_idc(chip, limit);
			break;
		default:
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
			break;
		}
	}
	break;

	case POWER_SUPPLY_PROP_HEALTH: {
		int battery_health_psy = val->intval;
		chip->btm_state = (battery_health_psy==POWER_SUPPLY_HEALTH_OVERHEAT) ? BTM_HEALTH_OVERHEAT :
			(battery_health_psy==POWER_SUPPLY_HEALTH_COLD) ? BTM_HEALTH_COLD : BTM_HEALTH_GOOD;
		pr_smb(PR_STATUS, "Update btm_state to %d\n", chip->btm_state);
	}
	break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
	}

	return rc;
}

static int smbchg_battery_set_property_post(struct power_supply *psy,
		enum power_supply_property prop, const union power_supply_propval *val,
		int rc) {
	return rc;
}

///////////////////////////////////////////////////////////////////////////////

int smbchg_battery_get_property_ext(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val) {

	int rc = smbchg_battery_get_property_pre(psy, prop, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smbchg_battery_get_property(psy, prop, val);
	rc = smbchg_battery_get_property_post(psy, prop, val, rc);

	return rc;
}

int smbchg_battery_set_property_ext(struct power_supply *psy,
		enum power_supply_property prop, const union power_supply_propval *val) {

	int rc = smbchg_battery_set_property_pre(psy, prop, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smbchg_battery_set_property(psy, prop, val);
	rc = smbchg_battery_set_property_post(psy, prop, val, rc);

	return rc;
}

int smbchg_battery_is_writeable_ext(struct power_supply *psy,
		enum power_supply_property prop) {
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_HEALTH:
		rc = 1;
		break;
	default:
		rc = smbchg_battery_is_writeable(psy, prop);
		break;
	}
	return rc;
}

enum power_supply_property* smbchg_battery_properties_ext(void) {
	int size_original = ARRAY_SIZE(smbchg_battery_properties);
	int size_appended = ARRAY_SIZE(smbchg_battery_properties_append);

	memcpy(_smbchg_battery_properties_ext, smbchg_battery_properties,
			size_original * sizeof(enum power_supply_property));
	memcpy(&_smbchg_battery_properties_ext[size_original],
			smbchg_battery_properties_append,
			size_appended * sizeof(enum power_supply_property));

	pr_smb(PR_STATUS, "show extended properties\n");
	{	int i;
		for (i = 0; i < ARRAY_SIZE(_smbchg_battery_properties_ext); ++i) {
			pr_smb(PR_STATUS, "%d : %d\n", i,
					_smbchg_battery_properties_ext[i]);
		}
	}

	return _smbchg_battery_properties_ext;
}

size_t smbchg_battery_num_properties_ext(void) {
	return ARRAY_SIZE(_smbchg_battery_properties_ext);
}

