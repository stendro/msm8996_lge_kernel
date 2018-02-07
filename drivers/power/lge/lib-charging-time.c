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
 *
 * README :
 * - The reporting value of charging remaining time for each SoC, is calculated by
 *	R_reporting(SoC) = R_profiled(SoC) * EMF(SoC);
 * - EMF means 'Exponential Moving Factor' and it is defined as
 *	SCoef := smooting coefficient, 0.02
 *	Trend(SoC) := (really elapsed time of SoC) / (profiled time of SoC)
 *	EMF(Start of SoC) := 1
 *   then,
 *	EMF(SoC) = SCoef * Trend(SoC-1) + (1-SCoef) * EMF(SoC-1);
 *
 * CHANGEs :
 * [2017-03-17] - I realize that EMF also does not consider the REAL charging pattern of
	dropping rate at the high level of battery. i.e, Charging under overtemp restricts
	the input current, then EMF can detect it by comparing measured and expected time
	(sec), but it would be effective ONLY ON low-mid battery level. In fact, limiting
	input current at the higher level does not affect charging time. But at the end of
	step charging or CV, the EMF value would overstate the 'Reporting' by its accumulated
	history even though the 'Trend' is stable to 1 now.
 */

#define pr_fmt(fmt) "CHARGING-TIME: %s: " fmt, __func__
#define pr_chgtime(reason, fmt, ...)			\
do {							\
	if (debug_mask & (reason))			\
		pr_info(fmt, ##__VA_ARGS__);		\
	else						\
		pr_debug(fmt, ##__VA_ARGS__);		\
} while (0)

#define CHARGING_TIME_COMPATIBLE	"lge,charging-time"
#define CHARGING_TIME_NAME		"lge-charging-time"

#define EMPTY				-1
#define NOTYET				-1

#include <linux/of.h>
#include <linux/slab.h>

#include "inc-charging-time.h"

enum debug_reason {
	ERROR = BIT(0),
	UPDATE = BIT(1),
	MONITOR = BIT(2),
	EVALUATE = BIT(3),

	VERBOSE = BIT(7),
} debug_mask = ERROR | UPDATE | MONITOR | EVALUATE | VERBOSE;

static const char* charging_domain [] = {
/* NIL */	"nil",
/* BC 1.2 */	"sdp", "cdp", "dcp",
/* HVDCPs */	"qc2", "qc3",
		"pd2", "pd3",
/* Misc */	"evp", "wlc"
};

static struct charging_time {
	/* smoothing_coefficient = smoothing_weight / smoothing_base */
	bool		emf_enabled;
	int		smoothing_weight;
	int		smoothing_base;
	/* overstatement_coefficient = weight / base */
	bool		overstatement_enabled;
	int		overstatement_weight;
	int		overstatement_base;

	/* static properties by charger profile */
	const char*	charger_type;
	unsigned int	charger_power;
	int		profile_consuming[100];
	int		profile_remaining[100+1];

	/* runtime properties */
	int		soc_begin;
	int		soc_now;
	int		runtime_emf;		// should be divided by smoothing_base
	int		runtime_consumed[100];
	int		runtime_remained[100+1];

	/* Timestamps */
	long		starttime_of_charging;
	long		starttime_of_soc;

	/* for debugging */
	int		evaluate_native[100];
	int		evaluate_reporting[100];
	int		evaluate_emf[100];	// should be divided by smoothing_base
} time_me = {
/* For EMF : weight for the latest delta */
	.emf_enabled = false,
	.smoothing_weight = 10000,
	.smoothing_base = 10000,
/* For Overstatement : overstated reporting factor */
	.overstatement_enabled = false,
	.overstatement_weight = 0,
	.overstatement_base = 100,

/* For beginning status */
	.charger_type = NULL,
	.charger_power = 0,
	.runtime_emf = EMPTY,

	.soc_now = NOTYET,
	.soc_begin = NOTYET,
	.starttime_of_charging = NOTYET,
	.starttime_of_soc = NOTYET,

/* Redundant initializing, but be sure to PRESERVE '_remains[100]' as '0' */
	.profile_remaining[100] = 0,
	.runtime_remained[100] = 0,
};

static struct device_node* dtree_get_charger(struct device_node* container,
	enum charger_type type) {

	struct device_node* child;

	const char* charger_queried = charging_domain[type];
	for_each_child_of_node(container, child) {
		const char* charger_type;

		if (of_property_read_string(child, "lge,charger-type", &charger_type))
			break;

		if (!strcmp(charger_queried, charger_type))
			return child;
	}

	return NULL;
}

static int dtree_get_compensator(struct device_node* container,
	bool* emf_enabled, int* smoothing_weight, int* smoothing_base,
	bool* overstatement_enabled, int* overstatement_weight, int* overstatement_base) {
	int buffer, rc = -1;

	if (!of_property_read_u32(container, "lge,emf-enable", &buffer)) {
		*emf_enabled = !!buffer;
		if (*emf_enabled) {
			if (of_property_read_u32(container, "lge,smoothing-weight", smoothing_weight))
				goto error;
			if (of_property_read_u32(container, "lge,smoothing-base", smoothing_base))
				goto error;
		}
	}
	else
		goto error;

	if (!of_property_read_u32(container, "lge,overstatement-enable", &buffer)) {
		*overstatement_enabled = !!buffer;
		if (*overstatement_enabled) {
			if (of_property_read_u32(container, "lge,overstatement-weight", overstatement_weight))
				goto error;
			if (of_property_read_u32(container, "lge,overstatement-base", overstatement_base))
				goto error;
		}
	}
	else
		goto error;

	return 0;

error:	*emf_enabled = false;
	*overstatement_enabled = false;
	return rc;
}

static struct device_node* dtree_get_nearest(struct device_node* container,
	int power, int* profile_consuming) {
#define DIFF(x,y) ((x)>=(y) ? (x)-(y) : INT_MAX)

	struct device_node* 	selected_node = NULL;
	unsigned int		selected_diff = INT_MAX;
	unsigned int		selected_power = INT_MAX;

	struct device_node* iter;
	int power_queried = power;
	for_each_child_of_node(container, iter) {
		int power_node;
		int power_diff;

		if (of_property_read_u32(iter, "lge,charger-power", &power_node))
			continue;

		power_diff = DIFF(power_node, power_queried);
		if (power_diff < selected_diff) {
			selected_node = iter;
			selected_diff = power_diff;
			selected_power = power_node;
		}
	}

	if (selected_node) {
		int i;
		const char* charger;

		if (of_property_read_string(selected_node, "lge,charger-type", &charger))
			return NULL;
		if (of_property_read_u32_array(selected_node, "lge,charger-profile", profile_consuming, 100))
			return NULL;

		for (i=0; i<100; i++) {
			int weight = (i < 80) ? 0 : 5*(i-80);

			profile_consuming[i] += profile_consuming[i]
					* (selected_power - power) / power	// Charging rate
					* (100 - weight) / 100;			// Reflecting weight
		}
		pr_chgtime(UPDATE, "Update charger power to %d, from %s\n", power, charger);
	}

	return selected_node;
}

static int dtree_get_profile(struct device_node* container, enum charger_type type, int power,
	unsigned int* profile_power, int* profile_consuming, int* profile_remaining) {

	int i, rc = -1;
	struct device_node* node = dtree_get_charger(container,
		type);

	/* Retrive charger power */
	if (of_property_read_u32(node, "lge,charger-power", profile_power))
		goto error;

	if (type == DCP && power < *profile_power) {
		*profile_power = power;

		if (!dtree_get_nearest(container, power, profile_consuming))
			goto error;
	}
	else {	/* Retrive profile array for 'profile_consuming' */
		if (of_property_read_u32_array(node, "lge,charger-profile", profile_consuming, 100))
			goto error;
	}

	/* Calculate 'profile_remaining' for each soc */
	for (i=99; 0<=i; --i)
		profile_remaining[i] = profile_consuming[i] + profile_remaining[i+1];

	/* Print for debugging */
	for (i=0; i<100; ++i)
		pr_chgtime(VERBOSE, "SoC %2d : Profiled remains : %5d\n", i, profile_remaining[i]);

	rc = 0;
error:	return rc;
}

static int runtime_paramter_trend(int soc) {
	// TODO : exception handling...

	// The returning value is multiplied by base to make it integer
	return time_me.runtime_consumed[soc] * time_me.smoothing_base
		/ time_me.profile_consuming[soc];
}

static int runtime_paramter_emf(int soc) {
	/* Do not update global variable, 'time_me' */

	// The returning value is multiplied by base to make it integer
	if (soc > time_me.soc_begin + 1) {
		int weight_of_trend = time_me.smoothing_weight;
		int weight_of_history = time_me.smoothing_base-time_me.smoothing_weight;
		int value_of_trend = runtime_paramter_trend(soc-1);
		int value_of_history = time_me.runtime_emf;

		return (weight_of_trend * value_of_trend + weight_of_history * value_of_history)
			/ time_me.smoothing_base;
	}
	else {
		// Returing default EMF at initial time and first estimation.
		return time_me.smoothing_base;
	}
}

static int remains_by_emf(int soc) {

	// This doesn't consider the decreasing SoC during charging
	if (time_me.runtime_remained[soc] == EMPTY) {
		int	remains_profiled;
		int	remains_emf;
		int	remains_result;

		// R_reporting(SoC) = R_profiled(SoC) * EMF(SoC);
		remains_profiled = time_me.profile_remaining[soc];
		remains_emf = runtime_paramter_emf(soc);
		remains_result = remains_profiled * remains_emf / time_me.smoothing_base;

		// Update runtime_emf and runtime_remained here!
		time_me.runtime_emf = time_me.evaluate_emf[soc] = remains_emf;
		time_me.runtime_remained[soc] = remains_result;

		pr_chgtime(MONITOR, "remains_result[%2d] = %5d = %d * %d \n",
			soc, remains_result, remains_profiled, remains_emf);
	}

	return time_me.runtime_remained[soc];
}

static int remains_by_native(int soc) {

	// How about the decreasing SoC during charging for native?
	if (time_me.evaluate_native[soc] == EMPTY) {
		#define NATIVE_DELTA_SOC 4
		int begin_soc = time_me.soc_begin;
		int delta_time = 0;
		int i;

		// Finding the begin of SoC
		for (i=0; i<100; ++i) {
			if (time_me.runtime_consumed[i] != EMPTY) {
				begin_soc = i;
				break;
			}
		}

		// Accumulating the delta times
		if (begin_soc != NOTYET && begin_soc + NATIVE_DELTA_SOC <= soc) {
			for (i=0; i<NATIVE_DELTA_SOC; ++i){
				delta_time += time_me.runtime_consumed[soc-1-i];
			}
			time_me.evaluate_native[soc] = (100-soc) * delta_time / NATIVE_DELTA_SOC;
		}

		pr_chgtime(MONITOR, "begin SoC %2d : Delta time : %5d\n", begin_soc, delta_time);
	}

	return time_me.evaluate_native[soc];
}

static void charging_time_evaluate(long eoc) {
	// Evaluation has meaning only on charging termination (== soc 100%)
	int i, begin_soc = time_me.soc_begin;
	int really_remained[100+1];

	if (begin_soc == 100) {
		/* If charging is started on 100%,
		 * Skip to evaluate
		 */
		return;
	}

	really_remained[100] = 0;
	for (i=99; begin_soc<=i; --i)
		really_remained[i] = time_me.runtime_consumed[i] + really_remained[i+1];

	pr_chgtime(EVALUATE, "Evaluating... %s charging from %2d(%ld) to 100(%ld), (duration %ld)\n",
		time_me.charger_type, begin_soc, time_me.starttime_of_charging, eoc, eoc-time_me.starttime_of_charging);

	pr_chgtime(EVALUATE, ", soc, really consumed, really remained"		/* really measured */
		", native remained, emf remained, reporting remained"		/* for comparison */
		", profiled remaining, profiled consuming, emf factor"		/* algorithm data */
		"\n");
	for (i=begin_soc; i<100; ++i) {
		pr_chgtime(EVALUATE, ", %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
			i, time_me.runtime_consumed[i], really_remained[i],
			time_me.evaluate_native[i], time_me.runtime_remained[i], time_me.evaluate_reporting[i],
			time_me.profile_remaining[i], time_me.profile_consuming[i], time_me.evaluate_emf[i]
		);
	}
}

int charging_time_remains(int soc) {

	// Simple check
	if ( !(time_me.charger_type && 0 <= soc && soc <= 100) ) {
		/* Invalid invokation */
		return NOTYET;
	}

	// This calling may NOT be bound with SoC changing
	if (time_me.soc_now != soc) {
		int remains_emf = NOTYET;
		int remains_native = NOTYET;

		long 		now;
		struct timespec	tspec;
		get_monotonic_boottime(&tspec);
		now = tspec.tv_sec;

		if (time_me.starttime_of_charging == EMPTY) {
			// New insertion
			time_me.soc_begin = soc;
			time_me.starttime_of_charging = now;
		}
		else {	// Soc rasing up
			time_me.runtime_consumed[soc-1] = now - time_me.starttime_of_soc;
		}

		/* Update time_me */
		time_me.soc_now = soc;
		time_me.starttime_of_soc = now;

		/* Retriving results for each algorithms */
		remains_emf = remains_by_emf(soc);
		remains_native = remains_by_native(soc);

		if (soc == 100) {
			/* Evaluate NOW! (at the 100% soc) :
			 * Evaluation has meaning only on full(100%) charged status
			 */
			charging_time_evaluate(now);
		}
		else {
			pr_chgtime(UPDATE, "soc %d, elapsed %ds... => emf %d, native %d\n",
				soc, time_me.runtime_consumed[soc > 0 ? soc-1 : 0],
				remains_emf, remains_native);
		}
	}

	/* At this time, the returning value is the result of profile_remaining(soc),
	 * and it is enlarged 2%. (2017-03-27)
	 */
	return time_me.evaluate_reporting[soc]
		= (time_me.emf_enabled ? time_me.runtime_remained[soc] : time_me.profile_remaining[soc])
		* (time_me.overstatement_base + (time_me.overstatement_enabled ? time_me.overstatement_weight : 0))
		/ time_me.overstatement_base;
}

void charging_time_clear(void) {
	int i;

	time_me.charger_type = NULL;
	time_me.charger_power = 0;

	// PRESERVE '_remains[100]' as '0'
	for (i=0; i<100; i++) {
		time_me.profile_consuming[i] = EMPTY;
		time_me.profile_remaining[i] = EMPTY;

		time_me.runtime_consumed[i] = EMPTY;
		time_me.runtime_remained[i] = EMPTY;

		time_me.evaluate_emf[i] = EMPTY;
		time_me.evaluate_reporting[i] = EMPTY;
		time_me.evaluate_native[i] = EMPTY;
	}
	time_me.runtime_emf = EMPTY;

	time_me.soc_begin = NOTYET;
	time_me.soc_now = NOTYET;
	time_me.starttime_of_charging = NOTYET;
	time_me.starttime_of_soc = NOTYET;
}

bool charging_time_initiate(enum charger_type type, int power /*mW*/) {
	/* If it needs to detect charger type by power further,
	   enhence this enumerating function
	 */

	const char* charger = charging_domain[type];
	struct device_node* container = of_find_compatible_node(NULL,
		NULL, CHARGING_TIME_COMPATIBLE);

	if (!container) {
		pr_chgtime(ERROR, "Check the device tree\n");
		goto fail;
	}

	if (charger != time_me.charger_type) {
		charging_time_clear();
		time_me.charger_type = charger;
		pr_chgtime(UPDATE, "Initiating for %s\n", charger);
	}
	else {
		pr_chgtime(UPDATE, "Initiating for same charger type %s\n", charger);
		goto skip;
	}

	if (dtree_get_compensator(container, &time_me.emf_enabled, &time_me.smoothing_weight, &time_me.smoothing_base,
		&time_me.overstatement_enabled, &time_me.overstatement_weight, &time_me.overstatement_base)) {
		pr_chgtime(ERROR, "Failed to parse compensators\n");
		goto fail;
	}

	if (dtree_get_profile(container, type, power,
		&time_me.charger_power, time_me.profile_consuming, time_me.profile_remaining)) {
		pr_chgtime(ERROR, "Failed to parse device tree for %s\n", charger);
		goto fail;
	}

skip :	return true;
fail :	return false;
}


