#ifndef __ANX7418_CHARGER_H__
#define __ANX7418_CHARGER_H__

#include <linux/power_supply.h>
#include "anx7418.h"

struct anx7418;

enum anx7418_charger_type {
	ANX7418_UNKNOWN_CHARGER = 0,
	ANX7418_CTYPE_CHARGER,
	ANX7418_CTYPE_PD_CHARGER,
};

struct anx7418_charger {
	struct anx7418 *anx;
	struct power_supply psy;
	struct power_supply_desc psy_d;

	bool is_otg;
	bool is_present;
	int curr_max;
	int volt_max;

	int ctype_charger;

	struct delayed_work chg_work;
	struct delayed_work vconn_work;
};

int anx7418_charger_init(struct anx7418 *anx);

#endif /* __ANX7418_CHARGER_H__ */
