/*
 * Customer HW 10 dependant file
 *
 * Copyright (C) 1999-2016, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_custom_lge.c 334946 2012-05-24 20:38:00Z $
 */
#ifdef CUSTOMER_HW10
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <dngl_stats.h>
#include <bcmutils.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_linux.h>
#include <bcmdevs.h>

#if defined(DHD_TCP_WINSIZE_ADJUST)
#include <linux/tcp.h>
#include <net/tcp.h>
#include <bcmendian.h>
#endif /* DHD_TCP_WINSIZE_ADJUST */

#ifdef SOFTAP_TPUT_ENHANCE
#include <dhd_ip.h>
#include <dhd_bus.h>
#endif /* SOFTAP_TPUT_ENHANCE */

#include <dhd_custom_lge.h>

#if defined(DHD_TCP_WINSIZE_ADJUST)
#define MIN_TCP_WIN_SIZE 18000
#define WIN_SIZE_SCALE_FACTOR 2
#define MAX_TARGET_PORTS 5
static uint target_ports[MAX_TARGET_PORTS] = {20, 0, 0, 0, 0};
uint dhd_use_tcp_window_size_adjust = FALSE;
#endif /* DHD_TCP_WINSIZE_ADJUST */


#if defined(DHD_TCP_WINSIZE_ADJUST)
int dhd_adjust_tcp_winsize(int index, int pk_type, int op_mode, struct sk_buff *skb);
#endif /* DHD_TCP_WINSIZE_ADJUST */

#ifdef CUSTOM_DSCP_TO_PRIO_MAPPING
extern int dhd_dscpmap_enable;
#endif

#if defined(OTP_WRITE_ON)
#include <dhd_custom_lge_otpbinary.h>
#endif /* OTP_WRITE_ON */

#ifdef LOGTRACE_FROM_FILE
#define LOGTRACEINFO "/data/logger/.logtrace.info"
#endif /* LOGTRACE_FROM_FILE */
struct cntry_locales_custom {
	char iso_abbrev[WLC_CNTRY_BUF_SZ]; /* ISO 3166-1 country abbreviation */
	char custom_locale[WLC_CNTRY_BUF_SZ]; /* Custom firmware locale */
	int32 custom_locale_rev; /* Custom local revisin default -1 */
};

/* Customized Locale table : OPTIONAL feature */
const struct cntry_locales_custom translate_custom_table[] = {
#if defined(BCM4330_CHIP) || defined(BCM4334_CHIP) || defined(BCM43241_CHIP)
	/* 4330/4334/43241 */
	{"AR", "AR", 1},
	{"AT", "AT", 1},
	{"AU", "AU", 2},
	{"BE", "BE", 1},
	{"BG", "BG", 1},
	{"BN", "BN", 1},
	{"CA", "CA", 2},
	{"CH", "CH", 1},
	{"CY", "CY", 1},
	{"CZ", "CZ", 1},
	{"DE", "DE", 3},
	{"DK", "DK", 1},
	{"EE", "EE", 1},
	{"ES", "ES", 1},
	{"FI", "FI", 1},
	{"FR", "FR", 1},
	{"GB", "GB", 1},
	{"GR", "GR", 1},
	{"HR", "HR", 1},
	{"HU", "HU", 1},
	{"IE", "IE", 1},
	{"IS", "IS", 1},
	{"IT", "IT", 1},
	{"JP", "JP", 5},
	{"KR", "KR", 24},
	{"KW", "KW", 1},
	{"LI", "LI", 1},
	{"LT", "LT", 1},
	{"LU", "LU", 1},
	{"LV", "LV", 1},
	{"MT", "MT", 1},
	{"NL", "NL", 1},
	{"NO", "NO", 1},
	{"PL", "PL", 1},
	{"PT", "PT", 1},
	{"PY", "PY", 1},
	{"RO", "RO", 1},
	{"RU", "RU", 13},
	{"SE", "SE", 1},
	{"SI", "SI", 1},
	{"SK", "SK", 1},
	{"TW", "TW", 2},
#ifdef BCM4330_CHIP
	{"",   "XZ", 1},	/* Universal if Country code is unknown or empty */
	{"IR", "XZ", 1},	/* Universal if Country code is IRAN, (ISLAMIC REPUBLIC OF) */
	{"SD", "XZ", 1},	/* Universal if Country code is SUDAN */
	{"SY", "XZ", 1},	/* Universal if Country code is SYRIAN ARAB REPUBLIC */
	{"GL", "XZ", 1},	/* Universal if Country code is GREENLAND */
	{"PS", "XZ", 1},	/* Universal if Country code is PALESTINIAN TERRITORY, OCCUPIED */
	{"TL", "XZ", 1},	/* Universal if Country code is TIMOR-LESTE (EAST TIMOR) */
	{"MH", "XZ", 1},	/* Universal if Country code is MARSHALL ISLANDS */
	{"JO", "XZ", 1},	/* Universal if Country code is Jordan */
	{"PG", "XZ", 1},	/* Universal if Country code is Papua New Guinea */
	{"SA", "XZ", 1},	/* Universal if Country code is Saudi Arabia */
	{"AF", "XZ", 1},	/* Universal if Country code is Afghanistan */
	{"US", "US", 5},
	{"UA", "UY", 0},
	{"AD", "AL", 0},
	{"CX", "AU", 2},
	{"GE", "GB", 1},
	{"ID", "MW", 0},
	{"KI", "AU", 2},
	{"NP", "SA", 0},
	{"WS", "SA", 0},
	{"LR", "BR", 0},
	{"ZM", "IN", 0},
	{"AN", "AG", 0},
	{"AI", "AS", 0},
	{"BM", "AS", 0},
	{"DZ", "GB", 1},
	{"LC", "AG", 0},
	{"MF", "BY", 0},
	{"GY", "CU", 0},
	{"LA", "GB", 1},
	{"LB", "BR", 0},
	{"MA", "IL", 0},
	{"MO", "BD", 0},
	{"MW", "BD", 0},
	{"QA", "BD", 0},
	{"TR", "GB", 1},
	{"TZ", "BF", 0},
	{"VN", "BR", 0},
	{"AE", "AZ", 0},
	{"IQ", "GB", 1},
	{"CN", "CL", 0},
	{"MX", "MX", 1},
#else
	/* 4334/43241 */
	{"",   "XZ", 11},	/* Universal if Country code is unknown or empty */
	{"IR", "XZ", 11},	/* Universal if Country code is IRAN, (ISLAMIC REPUBLIC OF) */
	{"SD", "XZ", 11},	/* Universal if Country code is SUDAN */
	{"SY", "XZ", 11},	/* Universal if Country code is SYRIAN ARAB REPUBLIC */
	{"GL", "XZ", 11},	/* Universal if Country code is GREENLAND */
	{"PS", "XZ", 11},	/* Universal if Country code is PALESTINIAN TERRITORY, OCCUPIED */
	{"TL", "XZ", 11},	/* Universal if Country code is TIMOR-LESTE (EAST TIMOR) */
	{"MH", "XZ", 11},	/* Universal if Country code is MARSHALL ISLANDS */
	{"SG", "SG", 4},
	{"US", "US", 46},
	{"UA", "UA", 8},
	{"CO", "CO", 4},
	{"ID", "ID", 1},
	{"LA", "LA", 1},
	{"LB", "LB", 2},
	{"VN", "VN", 4},
	{"MA", "MA", 1},
	{"TR", "TR", 7},
#endif /* defined(BCM4330_CHIP) */
#ifdef BCM4334_CHIP
	{"AE", "AE", 1},
	{"MX", "MX", 1},
#endif /* defined(BCM4334_CHIP) */
#ifdef BCM43241_CHIP
	{"AE", "AE", 6},
	{"BD", "BD", 2},
	{"CN", "CN", 38},
	{"MX", "MX", 20},
#endif /* defined(BCM43241_CHIP) */
#else  /* defined(BCM4330_CHIP) || defined(BCM4334_CHIP) || defined(BCM43241_CHIP) */
	/* default ccode/regrev */
	{"",   "XZ", 11},	/* Universal if Country code is unknown or empty */
	{"IR", "XZ", 11},	/* Universal if Country code is IRAN, (ISLAMIC REPUBLIC OF) */
	{"SD", "XZ", 11},	/* Universal if Country code is SUDAN */
	{"SY", "XZ", 11},	/* Universal if Country code is SYRIAN ARAB REPUBLIC */
	{"GL", "XZ", 11},	/* Universal if Country code is GREENLAND */
	{"PS", "XZ", 11},	/* Universal if Country code is PALESTINIAN TERRITORY, OCCUPIED */
	{"TL", "XZ", 11},	/* Universal if Country code is TIMOR-LESTE (EAST TIMOR) */
	{"MH", "XZ", 11},	/* Universal if Country code is MARSHALL ISLANDS */
	{"AL", "AL", 2},
	{"DZ", "DZ", 1},
	{"AS", "AS", 12},
	{"AI", "AI", 1},
	{"AG", "AG", 2},
	{"AR", "AR", 21},
	{"AW", "AW", 2},
	{"AU", "AU", 6},
	{"AT", "AT", 4},
	{"AZ", "AZ", 2},
	{"BS", "BS", 2},
	{"BH", "BH", 4},
	{"BD", "BD", 2},
	{"BY", "BY", 3},
	{"BE", "BE", 4},
	{"BM", "BM", 12},
	{"BA", "BA", 2},
	{"BR", "BR", 4},
	{"VG", "VG", 2},
	{"BN", "BN", 4},
	{"BG", "BG", 4},
	{"KH", "KH", 2},
	{"CA", "CA", 31},
	{"KY", "KY", 3},
	{"CN", "CN", 38},
	{"CO", "CO", 17},
	{"CR", "CR", 17},
	{"HR", "HR", 4},
	{"CY", "CY", 4},
	{"CZ", "CZ", 4},
	{"DK", "DK", 4},
	{"EE", "EE", 4},
	{"ET", "ET", 2},
	{"FI", "FI", 4},
	{"FR", "FR", 5},
	{"GF", "GF", 2},
	{"DE", "DE", 7},
	{"GR", "GR", 4},
	{"GD", "GD", 2},
	{"GP", "GP", 2},
	{"GU", "GU", 12},
	{"HK", "HK", 2},
	{"HU", "HU", 4},
	{"IS", "IS", 4},
	{"IN", "IN", 3},
	{"ID", "ID", 1},
	{"IE", "IE", 5},
	{"IL", "IL", 7},
	{"IT", "IT", 4},
	{"JP", "JP", 45},
	{"JO", "JO", 3},
	{"KW", "KW", 5},
	{"LA", "LA", 2},
	{"LV", "LV", 4},
	{"LB", "LB", 5},
	{"LS", "LS", 2},
	{"LI", "LI", 4},
	{"LT", "LT", 4},
	{"LU", "LU", 3},
	{"MO", "MO", 2},
	{"MK", "MK", 2},
	{"MW", "MW", 1},
	{"MY", "MY", 3},
	{"MV", "MV", 3},
	{"MT", "MT", 4},
	{"MQ", "MQ", 2},
	{"MR", "MR", 2},
	{"MU", "MU", 2},
	{"YT", "YT", 2},
	{"MX", "MX", 20},
	{"MD", "MD", 2},
	{"MC", "MC", 1},
	{"ME", "ME", 2},
	{"MA", "MA", 2},
	{"NP", "NP", 3},
	{"NL", "NL", 4},
	{"AN", "AN", 2},
	{"NZ", "NZ", 4},
	{"NO", "NO", 4},
	{"OM", "OM", 4},
	{"PA", "PA", 17},
	{"PG", "PG", 2},
	{"PY", "PY", 2},
	{"PE", "PE", 20},
	{"PH", "PH", 5},
	{"PL", "PL", 4},
	{"PT", "PT", 4},
	{"PR", "PR", 20},
	{"RE", "RE", 2},
	{"RO", "RO", 4},
	{"SN", "SN", 2},
	{"RS", "RS", 2},
	{"SG", "SG", 4},
	{"SK", "SK", 4},
	{"SI", "SI", 4},
	{"ES", "ES", 4},
	{"LK", "LK", 1},
	{"SE", "SE", 4},
	{"CH", "CH", 4},
	{"TW", "TW", 1},
	{"TH", "TH", 5},
	{"TT", "TT", 3},
	{"TR", "TR", 7},
	{"AE", "AE", 6},
	{"UG", "UG", 2},
	{"GB", "GB", 6},
	{"UY", "UY", 1},
	{"VI", "VI", 13},
	{"VA", "VA", 2},
	{"VE", "VE", 3},
	{"VN", "VN", 4},
	{"ZM", "ZM", 2},
	{"EC", "EC", 21},
	{"SV", "SV", 25},
	{"KR", "KR", 48},
	{"RU", "RU", 13},
	{"UA", "UA", 8},
	{"GT", "GT", 1},
	{"FR", "FR", 5},
	{"MN", "MN", 1},
	{"NI", "NI", 2},
#endif /* default ccode/regrev */
};

/* Customized Locale convertor
*  input : ISO 3166-1 country abbreviation
*  output: customized cspec
*/
#ifdef CUSTOM_COUNTRY_CODE
void get_customized_country_code(void *adapter, char *country_iso_code,
  wl_country_t *cspec, u32 flags)
#else
void get_customized_country_code(void *adapter, char *country_iso_code, wl_country_t *cspec)
#endif /* CUSTOM_COUNTRY_CODE */
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))

	struct cntry_locales_custom *cloc_ptr;

	if (!cspec)
		return;

#ifdef CUSTOM_COUNTRY_CODE
	cloc_ptr = wifi_platform_get_country_code(adapter, country_iso_code, flags);
#else
	cloc_ptr = wifi_platform_get_country_code(adapter, country_iso_code);
#endif
	if (cloc_ptr) {
		strlcpy(cspec->ccode, cloc_ptr->custom_locale, WLC_CNTRY_BUF_SZ);
		cspec->rev = cloc_ptr->custom_locale_rev;
	}
	return;
#else
	int size, i;

	size = ARRAYSIZE(translate_custom_table);

	if (cspec == 0)
		 return;

	if (size == 0)
		 return;

	for (i = 0; i < size; i++) {
		if (strcmp(country_iso_code, translate_custom_table[i].iso_abbrev) == 0) {
			memcpy(cspec->ccode,
				translate_custom_table[i].custom_locale, WLC_CNTRY_BUF_SZ);
			cspec->rev = translate_custom_table[i].custom_locale_rev;
			return;
		}
	}
	return;
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)) */
}
#if defined(BCM4359_CHIP)
#define CIS_BUF_SIZE            1280
#else
#define CIS_BUF_SIZE            512
#endif /* BCM4359_CHIP */

#ifdef OTP_WRITE_ON
#define htod32(i) (i)

int dhd_write_otp(dhd_pub_t *dhd)
{
	int ret;
	char buf[CIS_BUF_SIZE] = {0};
	char *bufp;
	const char *otp_vars;
	int otp_header_size;
	int otp_vars_size;
	uint32 len = 0;
	uint chipid;
	cis_rw_t cish;
	char *cisp, *cisdata;
	int max = 0;
	cis_rw_t *cish_r = (cis_rw_t *)&buf[8];

	chipid = dhd_bus_chip_id(dhd);
	DHD_INFO(("%s: chipid = 0x%x\n", __FUNCTION__, chipid));
	if (CHIPID(chipid) == BCM4359_CHIP_ID) {
		otp_vars = BCM43596_otp_vars;
		otp_vars_size = sizeof(BCM43596_otp_vars);
		otp_header_size = BCM43596_OTP_HEADER_SIZE;
	} else {
		DHD_ERROR(("%s: can't find OTP header for chip\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (otp_vars_size >= CIS_BUF_SIZE) {
		DHD_ERROR(("%s: CIS BUF is too small\n", __FUNCTION__));
		return BCME_ERROR;
	}

	cish_r->source = 0;
	cish_r->byteoff = 0;
	cish_r->nbytes = sizeof(buf);

	strcpy(buf, "cisdump");
	ret = dhd_wl_ioctl_cmd(dhd, WLC_GET_VAR, buf, sizeof(buf), 0, 0);
	if (ret < 0) {
		DHD_ERROR(("[WIFI_LGE] %s: CIS reading failed, ret=%d\n",
			__FUNCTION__, ret));
		goto exit;
	}
	max = otp_header_size;
	if (memcmp(&buf[12], otp_vars, otp_header_size) == 0) {
		DHD_ERROR(("%s:OTP already was written\n", __FUNCTION__));
	} else {
		DHD_ERROR(("%s: OTP length: %d bytes \n", __FUNCTION__, otp_vars_size));

		max = sizeof(buf);
		bufp = buf;
		memset(buf, 0, sizeof(buf));
		strcpy(bufp, "ciswrite");
		bufp += strlen("ciswrite") + 1;
		cisp = bufp;
		cisdata = cisp + sizeof(cish);

		cish.source = htod32(0);

		memcpy(cisdata, (char *)otp_vars, otp_vars_size);
		len = otp_vars_size;

		cish.byteoff = htod32(0);
		cish.nbytes = htod32(len);
		memcpy(cisp, (char*)&cish, sizeof(cish));

		ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_VAR, buf,
			(cisp - buf) + sizeof(cish) + len, TRUE, 0);
		if (ret) {
			DHD_ERROR(("%s: Fail to write otp, ret = %d \n", __FUNCTION__, ret));
		} else {
			DHD_ERROR(("Success to write otp \n"));
		}
	}
	/* check or create .otp.info */
exit:
	return ret;
}
#endif /* OTP_WRITE_ON */

#ifdef CONFIG_CONTROL_PM
void dhd_control_pm(dhd_pub_t *dhd, uint *power_mode)
{
	return;
}
#endif /* CONFIG_CONTROL_PM */
#if defined(DHD_TCP_WINSIZE_ADJUST)
static int dhd_port_list_match(int port)
{
	int i;
	for (i = 0; i < MAX_TARGET_PORTS; i++) {
		if (target_ports[i] == port)
			return 1;
	}
	return 0;
}
int dhd_adjust_tcp_winsize(int index, int pk_type, int op_mode, struct sk_buff *skb)
{

	struct iphdr *ipheader;
	struct tcphdr *tcpheader;
	uint16 win_size;
	int32 incremental_checksum;

	if (!dhd_use_tcp_window_size_adjust || !(op_mode & DHD_FLAG_HOSTAP_MODE))
		return 0;

	if (skb == NULL || skb->data == NULL)
		return 0;


	if (index == 0 || pk_type == ETHER_TYPE_IP) {

		ipheader = (struct iphdr*)(skb->data);

		if (ipheader->protocol == IPPROTO_TCP) {
			tcpheader = (struct tcphdr*) skb_pull(skb, (ipheader->ihl)<<2);
			if (tcpheader) {
				win_size = ntoh16(tcpheader->window);
				if (win_size < MIN_TCP_WIN_SIZE &&
					dhd_port_list_match(ntoh16(tcpheader->dest))) {
					incremental_checksum = ntoh16(tcpheader->check);
					incremental_checksum += win_size - win_size
							*WIN_SIZE_SCALE_FACTOR;
					if (incremental_checksum < 0)
						--incremental_checksum;
					tcpheader->window = hton16(win_size*WIN_SIZE_SCALE_FACTOR);
					tcpheader->check = hton16((unsigned short)
							incremental_checksum);
				}
			}
			skb_push(skb, (ipheader->ihl)<<2);
		}
	}
	return 0;
}
#endif /* DHD_TCP_WINSIZE_ADJUST */

int set_parallelscan(dhd_pub_t *dhd)
{
	uint32 scan_parallel;
	int ret = 0;

	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
		scan_parallel = 0;
	} else {
		scan_parallel = 1;
	}
	DHD_ERROR(("set_parallelscan op_mode = 0x%04x, scan_parallel = %d\n",
		dhd->op_mode, scan_parallel));
	ret = dhd_iovar(dhd, 0, "scan_parallel", (char *)&scan_parallel, sizeof(scan_parallel),
		NULL, 0, TRUE);

	return ret;
}

#ifdef SOFTAP_TPUT_ENHANCE
int set_softap_params(dhd_pub_t *dhd)
{
	uint32 iovar_set;
	int ret = 0;
	if (dhd->op_mode & DHD_FLAG_HOSTAP_MODE) {
#ifdef BCMSDIO
		dhd_bus_setidletime(dhd, 100);
#endif

#ifdef DHDTCPACK_SUPPRESS
#ifndef SET_TCPACK_SUPPRESS
		dhd_tcpack_suppress_set(dhd, TCPACK_SUP_OFF);
#endif
#endif

#if defined(DHD_TCP_WINSIZE_ADJUST)
		dhd_use_tcp_window_size_adjust = TRUE;
#endif

#ifdef CUSTOM_DSCP_TO_PRIO_MAPPING
		dhd_dscpmap_enable = 1;
#endif

		iovar_set = 10;
		dhd_iovar(dhd, 0, "ampdu_retry_limit", (char *)&iovar_set, sizeof(iovar_set), NULL, 0, TRUE);

		iovar_set = 5;
		dhd_iovar(dhd, 0, "ampdu_rr_retry_limit", (char *)&iovar_set, sizeof(iovar_set), NULL, 0, TRUE);

		iovar_set = 13;
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_SRL, (char *)&iovar_set,
			sizeof(iovar_set), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set SRL failed  %d\n", __FUNCTION__, ret));
		}

		iovar_set = 13;
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_LRL, (char *)&iovar_set,
			sizeof(iovar_set), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set LRL failed  %d\n", __FUNCTION__, ret));
		}
		// Update frameburst to 1 to meet SoftAP T.P requirement for BELL operator, BRCM Case 1112892
		iovar_set = 1;
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_FAKEFRAG, (char *)&iovar_set,
			sizeof(iovar_set), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set frameburst failed  %d\n", __FUNCTION__, ret));
		}
	} else {
#ifdef CUSTOM_DSCP_TO_PRIO_MAPPING
		dhd_dscpmap_enable = 0;
#endif
#ifdef DHDTCPACK_SUPPRESS
#ifndef SET_TCPACK_SUPPRESS
       dhd_tcpack_suppress_set(dhd, TCPACK_SUP_HOLD);
#endif
#endif
		iovar_set = 7;
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_SRL, (char *)&iovar_set,
			sizeof(iovar_set), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set SRL failed  %d\n", __FUNCTION__, ret));
		}

		iovar_set = 6;
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_SET_LRL, (char *)&iovar_set,
			sizeof(iovar_set), TRUE, 0)) < 0) {
			DHD_ERROR(("%s Set LRL failed  %d\n", __FUNCTION__, ret));
		}
	}
	return ret;
}
#endif /* SOFTAP_TPUT_ENHANCE */

#ifdef LOGTRACE_FROM_FILE
/*
 * LOGTRACEINFO = /data/.logtrace.info
 *  - logtrace = 1            => Enable LOGTRACE Event
 *  - logtrace = 0            => Disable LOGTRACE Event
 *  - file not exist          => Disable LOGTRACE Event
 */
int dhd_logtrace_from_file(dhd_pub_t *dhd)
{
	struct file *fp = NULL;
	int ret = -1;
	uint32 logtrace = 0;
	char *filepath = LOGTRACEINFO;

	/* Read LOGTRACE Event on/off request from the file */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("[WIFI_LGE] %s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
		return 0;
	} else {
		ret = kernel_read(fp, 0, (char *)&logtrace, 4);
		if (ret < 0) {
			DHD_ERROR(("[WIFI_LGE] %s: File read error, ret=%d\n", __FUNCTION__, ret));
			filp_close(fp, NULL);
			return 0;
		}

		logtrace = bcm_atoi((char *)&logtrace);

		DHD_ERROR(("[WIFI_LGE] %s: LOGTRACE On/Off from file = %d\n",
			__FUNCTION__, logtrace));
		filp_close(fp, NULL);

		/* Check value from the file */
		if (logtrace > 2) {
			DHD_ERROR(("[WIFI_LGE] %s: Invalid value %d read from the file %s\n",
				__FUNCTION__, logtrace, filepath));
			return 0;
		}
	}

	return (int)logtrace;
}
#endif /* LOGTRACE_FROM_FILE */
#endif /* CUSTOMER_HW10 */
