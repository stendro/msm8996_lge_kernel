/*
 * LGE charging scenario.
 *
 * Copyright (C) 2013 LG Electronics
 * mansu.lee <mansu.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <soc/qcom/lge/lge_charging_scenario.h>
#include <linux/string.h>

/* For LGE charging scenario debug */
#ifdef DEBUG_LCS
/* For fake battery temp' debug */
#ifdef DEBUG_LCS_DUMMY_TEMP
static int dummy_temp = 250;
static int time_order = 1;
#endif
#endif

#define CHG_MAXIDX	7

static struct batt_temp_table chg_temp_table[CHG_MAXIDX] = {
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FOR_SPRINT
	{INT_MIN,         -51,    CHG_BATTEMP_BL_UT},
	{     -50,        -20,    CHG_BATTEMP_M5_M2},
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
	{     -19,        390,    CHG_BATTEMP_M2_39},
	{     391,        420,    CHG_BATTEMP_40_42},
	{     421,        500,    CHG_BATTEMP_43_50},
#else
	{     -19,        420,    CHG_BATTEMP_M2_42},
	{     421,        450,    CHG_BATTEMP_42_45},
	{     451,        500,    CHG_BATTEMP_45_50},
#endif
	{     501,        530,    CHG_BATTEMP_50_OT},
	{     531,    INT_MAX,    CHG_BATTEMP_AB_OT},
#else
	{INT_MIN,        -101,    CHG_BATTEMP_BL_UT},
	{    -100,        -50,    CHG_BATTEMP_M10_M5},
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
	{     -49,        390,    CHG_BATTEMP_M4_39},
	{     391,        420,    CHG_BATTEMP_40_42},
	{     421,        520,    CHG_BATTEMP_43_51},
#else
	{     -49,        430,    CHG_BATTEMP_M5_43},
	{     431,        450,    CHG_BATTEMP_43_45},
	{     451,        520,    CHG_BATTEMP_45_52},
#endif
	{     521,        550,    CHG_BATTEMP_52_OT},
	{     551,    INT_MAX,    CHG_BATTEMP_AB_OT},
#endif
};

#define IS_EXTREME_H_OR_L_TEMP(battemp_st)	\
	((battemp_st) == CHG_BATTEMP_AB_OT || (battemp_st) == CHG_BATTEMP_BL_UT) ? 1 : 0

static enum lge_charging_states charging_state;
static enum lge_states_changes states_change;
static int change_charger;
static int pseudo_chg_ui;

#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
static int last_thermal_current;
#endif

#ifdef CONFIG_LGE_ADJUST_BATT_TEMP
#define MAX_BATT_TEMP_CHECK_COUNT 2
static int adjust_batt_temp(int batt_temp)
{
	static int prev_batt_temp = 250;
	static int count = 1;

	pr_info("before adjust batt_temp = %d\n", batt_temp);

	if (batt_temp >= 400 && batt_temp <= 500
		&& batt_temp - prev_batt_temp > -20
		&& batt_temp - prev_batt_temp < 30) {

		if (batt_temp == prev_batt_temp)
			count++;

		if (count >= MAX_BATT_TEMP_CHECK_COUNT) {
			/* use the current temp */
			count = 1;
		} else {
			/* use the previous temp */
			batt_temp = prev_batt_temp;
		}

	} else {
		count = 1;
	}
	prev_batt_temp = batt_temp;

	return batt_temp;
}
#endif

static enum lge_battemp_states determine_batt_temp_state(int batt_temp)
{
	int cnt;

#ifdef CONFIG_LGE_ADJUST_BATT_TEMP
	batt_temp = adjust_batt_temp(batt_temp);
#endif

	/* Decrease order */
	for (cnt = (CHG_MAXIDX-1); 0 <= cnt; cnt--) {
		if (chg_temp_table[cnt].min <= batt_temp &&
			batt_temp <= chg_temp_table[cnt].max)
			break;
	}

	if (cnt < 0)  //WBT cnt => -1 value case.
		cnt = 0;

	return chg_temp_table[cnt].battemp_state;
}

static enum lge_charging_states
determine_lge_charging_state(enum lge_battemp_states battemp_st, int batt_volt)
{
	enum lge_charging_states next_state = charging_state;
	states_change = STS_CHE_NONE;

	/* Determine next charging status Based on previous status */
	switch (charging_state) {
	case CHG_BATT_NORMAL_STATE:
		if (IS_EXTREME_H_OR_L_TEMP(battemp_st)) {
			states_change = STS_CHE_NORMAL_TO_STPCHG;
			pseudo_chg_ui = 0;
			next_state = CHG_BATT_STPCHG_STATE;
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FOR_SPRINT
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		} else if ( battemp_st >= CHG_BATTEMP_43_50 ) {
#else
		} else if ( battemp_st >= CHG_BATTEMP_45_50 ) {
#endif
#else
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		} else if ( battemp_st >= CHG_BATTEMP_43_51 ) {
#else
		} else if ( battemp_st >= CHG_BATTEMP_45_52 ) {
#endif
#endif
			if (batt_volt >= DC_IUSB_VOLTUV) {
				states_change = STS_CHE_NORMAL_TO_STPCHG;
				next_state = CHG_BATT_STPCHG_STATE;
			} else {
				states_change = STS_CHE_NORMAL_TO_DECCUR;
				next_state = CHG_BATT_DECCUR_STATE;
			}
			pseudo_chg_ui = 1;
		}
		break;
	case CHG_BATT_DECCUR_STATE:
		if (IS_EXTREME_H_OR_L_TEMP(battemp_st)){
			states_change = STS_CHE_DECCUR_TO_STPCHG;
			pseudo_chg_ui = 0;
			next_state = CHG_BATT_STPCHG_STATE;
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FOR_SPRINT
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		} else if (battemp_st < CHG_BATTEMP_40_42) {
#else
		} else if (battemp_st < CHG_BATTEMP_42_45) {
#endif
#else
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		} else if (battemp_st < CHG_BATTEMP_40_42) {
#else
		} else if (battemp_st < CHG_BATTEMP_43_45) {
#endif
#endif
			states_change = STS_CHE_DECCUR_TO_NORMAL;
			next_state = CHG_BATT_NORMAL_STATE;
			pseudo_chg_ui = 1;
		} else if (batt_volt > DC_IUSB_VOLTUV) {
			states_change = STS_CHE_DECCUR_TO_STPCHG;
			next_state = CHG_BATT_STPCHG_STATE;
			pseudo_chg_ui = 1;
		}
		break;
	case CHG_BATT_WARNIG_STATE:
		break;
	case CHG_BATT_STPCHG_STATE:

		if (IS_EXTREME_H_OR_L_TEMP(battemp_st)) {
			pseudo_chg_ui = 0;
		}
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FOR_SPRINT
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		else if (battemp_st == CHG_BATTEMP_M2_39 ||
			battemp_st == CHG_BATTEMP_40_42) {
#else
		else if (battemp_st == CHG_BATTEMP_M2_42 ||
			battemp_st == CHG_BATTEMP_42_45) {
#endif
#else
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		else if (battemp_st == CHG_BATTEMP_M4_39) {
#else
		else if (battemp_st == CHG_BATTEMP_M5_43) {
#endif
#endif
			states_change = STS_CHE_STPCHG_TO_NORMAL;
			pseudo_chg_ui = 1;
			next_state = CHG_BATT_NORMAL_STATE;
		}
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FOR_SPRINT
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		else if (battemp_st == CHG_BATTEMP_43_50) {
#else
		else if (battemp_st == CHG_BATTEMP_45_50) {
#endif
#else
#ifdef CONFIG_LGE_PM_OTP_SCENARIO_FAST_LIMIT
		else if (battemp_st == CHG_BATTEMP_40_42 ||
			battemp_st ==  CHG_BATTEMP_43_51) {
#else
		else if (battemp_st == CHG_BATTEMP_43_45 ||
			battemp_st ==  CHG_BATTEMP_45_52) {
#endif
#endif
			if (batt_volt < DC_IUSB_VOLTUV) {
				states_change = STS_CHE_STPCHG_TO_DECCUR;
				next_state = CHG_BATT_DECCUR_STATE;
			}
			pseudo_chg_ui = 1;
		}
		break;
	default:
		pr_err("unknown charging status. %d\n", charging_state);
		break;
	}

	pr_info("determine_lge_charging_state : states_change[%d], next_state[%d], pseudo_chg_ui[%d]\n",
			states_change, next_state, pseudo_chg_ui);

	return next_state;
}

void lge_monitor_batt_temp(struct charging_info req, struct charging_rsp *res)
{
	enum lge_battemp_states battemp_state;
	enum lge_charging_states pre_state;
#ifdef DEBUG_LCS
#ifdef DEBUG_LCS_DUMMY_TEMP
	if (time_order == 1) {
		dummy_temp = dummy_temp + 10;
		if (dummy_temp > 650)
			time_order = 0;
	} else {
		dummy_temp = dummy_temp - 10;
		if (dummy_temp < -150)
			time_order = 1;
	}

	req.batt_temp = dummy_temp;
#endif
#endif

	if (change_charger ^ req.is_charger) {
		change_charger = req.is_charger;
		if (req.is_charger) {
			charging_state = CHG_BATT_NORMAL_STATE;
			res->force_update = true;
		} else
			res->force_update = false;
	} else {
		res->force_update = false;
	}

	pre_state = charging_state;

	battemp_state =
		determine_batt_temp_state(req.batt_temp);
	charging_state =
		determine_lge_charging_state(battemp_state, req.batt_volt);

	res->state = charging_state;
	res->change_lvl = states_change;
	res->disable_chg =
		charging_state == CHG_BATT_STPCHG_STATE ? true : false;

#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	if (charging_state == CHG_BATT_NORMAL_STATE) {
		if (req.chg_current_te <= req.chg_current_ma)
			res->chg_current = req.chg_current_te;
		else
			res->chg_current = req.chg_current_ma;
	} else if (charging_state == CHG_BATT_DECCUR_STATE) {
		if (req.chg_current_te <= DC_IUSB_CURRENT)
			res->chg_current = req.chg_current_te;
		else
			res->chg_current = DC_IUSB_CURRENT;
	} else {
		res->chg_current = DC_CURRENT_DEF;
	}

	if (last_thermal_current ^ res->chg_current) {
		last_thermal_current = res->chg_current;
		res->force_update = true;
	}
#else
	res->chg_current =
		charging_state ==
		CHG_BATT_DECCUR_STATE ? DC_IUSB_CURRENT : DC_CURRENT_DEF;
#endif

	res->btm_state = BTM_HEALTH_GOOD;

	if (battemp_state >= CHG_BATTEMP_AB_OT)
		res->btm_state = BTM_HEALTH_OVERHEAT;
	else if (battemp_state <= CHG_BATTEMP_BL_UT)
		res->btm_state = BTM_HEALTH_COLD;
	else
		res->btm_state = BTM_HEALTH_GOOD;

	res->pseudo_chg_ui = pseudo_chg_ui;

#ifdef DEBUG_LCS
	pr_err("DLCS ==============================================\n");
#ifdef DEBUG_LCS_DUMMY_TEMP
	pr_err("DLCS : dummy battery temperature  = %d\n", dummy_temp);
#endif
	pr_err("DLCS : battery temperature states = %d\n", battemp_state);
	pr_err("DLCS : res -> state        = %d\n", res->state);
	pr_err("DLCS : res -> change_lvl   = %d\n", res->change_lvl);
	pr_err("DLCS : res -> force_update = %d\n", res->force_update ? 1 : 0);
	pr_err("DLCS : res -> chg_disable  = %d\n", res->disable_chg ? 1 : 0);
	pr_err("DLCS : res -> chg_current   = %d\n", res->chg_current);
	pr_err("DLCS : res -> btm_state    = %d\n", res->btm_state);
	pr_err("DLCS : res -> is_charger   = %d\n", req.is_charger);
	pr_err("DLCS : res -> pseudo_chg_ui= %d\n", res->pseudo_chg_ui);
#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	pr_err("DLCS : req -> chg_current  = %d\n", req.chg_current_te);
#endif
	pr_err("DLCS ==============================================\n");
#endif

#ifdef CONFIG_LGE_THERMALE_CHG_CONTROL
	pr_err("LGE charging scenario : state %d -> %d(%d-%d),"\
		" temp=%d, volt=%d, BTM=%d,"\
		" charger=%d, cur_set=%d/%d, chg_cur = %d\n",
		pre_state, charging_state, res->change_lvl,
		res->force_update ? 1 : 0,
		req.batt_temp, req.batt_volt / 1000,
		res->btm_state, req.is_charger,
		req.chg_current_te, res->chg_current, req.current_now);
#else
	pr_err("LGE charging scenario : state %d -> %d(%d-%d),"\
		" temp=%d, volt=%d, BTM=%d,"\
		" charger=%d, chg_cur = %d\n",
		pre_state, charging_state, res->change_lvl,
		res->force_update ? 1 : 0,
		req.batt_temp, req.batt_volt / 1000,
		res->btm_state, req.is_charger, req.current_now);
#endif
}


