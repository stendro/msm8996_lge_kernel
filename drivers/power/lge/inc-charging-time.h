#ifndef _INC_CHARGING_INFORMATION_H_
#define _INC_CHARGING_INFORMATION_H_

enum charger_type {
	NIL,
	SDP,
	CDP,
	DCP,
	QC2,
	QC3,
	PD2,
	PD3,
	EVP,
	WLC,
};

bool charging_time_initiate(enum charger_type type, int power);
int  charging_time_remains(int soc);
void charging_time_clear(void);

#endif
