/*
 *  drivers/media/radio/rtc6213n/radio-rtc6213n.h
 *
 *  Driver for Richwave RTC6213N FM Tuner
 *
 *  Copyright (c) 2009 Tobias Lorenz <tobias.lorenz@gmx.net>
 *  Copyright (c) 2012 Hans de Goede <hdegoede@redhat.com>
 *  Copyright (c) 2013 Richwave Technology Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/* driver definitions */
/* #define _RDSDEBUG */
#define DRIVER_NAME "rtc6213n-fmtuner"

/* kernel includes */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <asm/unaligned.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/kfifo.h>
#include <asm/unaligned.h>

/* driver definitions */
#define DRIVER_KERNEL_VERSION KERNEL_VERSION(1, 0, 1)
#define DRIVER_CARD "Richwave rtc6213n FM Tuner"
#define DRIVER_DESC "I2C radio driver for rtc6213n FM Tuner"
#define DRIVER_VERSION "1.0.1"

/**************************************************************************
 * Register Definitions
 **************************************************************************/
#define RADIO_REGISTER_SIZE         2       /* 16 register bit width */
#define RADIO_REGISTER_NUM          16      /* DEVICEID */
#define RDS_REGISTER_NUM            6       /* STATUSRSSI */

#define DEVICEID                    0       /* Device ID Code */
#define DEVICE_ID                   0xffff  /* [15:00] Device ID */
#define DEVICEID_PN                 0xf000  /* [15:12] Part Number */
#define DEVICEID_MFGID              0x0fff  /* [11:00] Manufacturer ID */

#define CHIPID                      1       /* Chip ID Code */
#define CHIPID_REVISION_NO          0xfc00  /* [15:10] Chip Reversion */

#define MPXCFG                      2       /* Power Configuration */
#define MPXCFG_CSR0_DIS_SMUTE       0x8000  /* [15:15] Disable Softmute */
#define MPXCFG_CSR0_DIS_MUTE        0x4000  /* [14:14] Disable Mute */
#define MPXCFG_CSR0_MONO            0x2000  /* [13:13] Mono or Auto Detect */
#define MPXCFG_CSR0_DEEM            0x1000  /* [12:12] DE-emphasis */
#define MPXCFG_CSR0_BLNDADJUST      0x0300  /* [09:08] Blending Adjust */
#define MPXCFG_CSR0_SMUTERATE       0x00c0  /* [07:06] Softmute Rate */
#define MPXCFG_CSR0_SMUTEATT        0x0030  /* [05:04] Softmute Attenuation */
#define MPXCFG_CSR0_VOLUME          0x000f  /* [03:00] Volume */

#define CHANNEL                     3       /* Tuning Channel Setting */
#define CHANNEL_CSR0_TUNE           0x8000  /* [15:15] Tune */
#define CHANNEL_CSR0_BAND           0x3000  /* [13:12] Band Select */
#define CHANNEL_CSR0_CHSPACE        0x0c00  /* [11:10] Channel Sapcing */
#define CHANNEL_CSR0_CH             0x03ff  /* [09:00] Tuning Channel */

#define SYSCFG                      4       /* System Configuration 1 */
#define SYSCFG_CSR0_RDSIRQEN        0x8000  /* [15:15] RDS Interrupt Enable */
#define SYSCFG_CSR0_STDIRQEN        0x4000  /* [14:14] STD Interrupt Enable */
#define SYSCFG_CSR0_DIS_AGC         0x2000  /* [13:13] Disable AGC */
#define SYSCFG_CSR0_RDS_EN          0x1000  /* [12:12] RDS Enable */
#define SYSCFG_CSR0_RBDS_M          0x0300  /* [09:08] MMBS setting */

#define SEEKCFG1                    5       /* Seek Configuration 1 */
#define SEEKCFG1_CSR0_SEEK          0x8000  /* [15:15] Enable Seek Function */
#define SEEKCFG1_CSR0_SEEKUP        0x4000  /* [14:14] Seek Direction */
#define SEEKCFG1_CSR0_SKMODE        0x2000  /* [13:13] Seek Mode */
#define SEEKCFG1_CSR0_SEEKRSSITH    0x00ff  /* [07:00] RSSI Seek Threshold */

#define POWERCFG                    6       /* Power Configuration */
#define POWERCFG_CSR0_ENABLE        0x8000  /* [15:15] Power-up Enable */
#define POWERCFG_CSR0_DISABLE       0x4000  /* [14:14] Power-up Disable */
#define POWERCFG_CSR0_BLNDOFS       0x0f00  /* [11:08] Blending Offset Value */

#define PADCFG                      7       /* PAD Configuration */
#define PADCFG_CSR0_RC_EN           0xf000  /* Internal crystal RC enable*/
#define PADCFG_CSR0_GPIO            0x0004  /* [03:02] General purpose I/O */

#define BANKCFG                     8       /* Bank Serlection */
/* contains reserved */

#define SEEKCFG2                    9       /* Seek Configuration 2 */
#define SEEKCFG2_CSR0_OFSTH         0xff00  /* [15:08] DC offset fail TH */
#define SEEKCFG2_CSR0_QLTTH         0x00ff  /* [07:00] Spike fail TH */

/* BOOTCONFIG only contains reserved [*/
#define STATUS                      10      /* Status and Work channel */
#define STATUS_RDS_RDY              0x8000  /* [15:15] RDS Ready */
#define STATUS_STD                  0x4000  /* [14:14] Seek/Tune Done */
#define STATUS_SF                   0x2000  /* [13:13] Seek Fail */
#define STATUS_RDS_SYNC             0x0800  /* [11:11] RDS synchronization */
#define STATUS_SI                   0x0400  /* [10:10] Stereo Indicator */
#define STATUS_READCH               0x03ff  /* [09:00] Read Channel */

#define RSSI                        11      /* RSSI and RDS error */
#define RDS_BA_ERRS            0xc000  /* [15:14] RDS Block A Errors */
#define RDS_BB_ERRS            0x3000  /* [13:12] RDS Block B Errors */
#define RDS_BC_ERRS            0x0c00  /* [11:10] RDS Block C Errors */
#define RDS_BD_ERRS            0x0300  /* [9:8] RDS Block D Errors */
#define RSSI_RSSI                   0x00ff  /* [7:0] Read Channel */

#define BA_DATA                     12      /* Block A data */
#define RDSA                   0xffff  /* [15:00] RDS Block A Data */

#define BB_DATA                     13      /* Block B data */
#define RDSB                  0xffff  /* [15:00] RDS Block B Data */

#define BC_DATA                     14      /* Block C data */
#define RDSC                0xffff  /* [15:00] RDS Block C Data */

#define BD_DATA                     15      /* Block D data */
#define RDSD                   0xffff  /* [15:00] RDS Block D Data */

#define TURN_ON 1
#define TURN_OFF 0
#define SRCH_UP          1
#define SRCH_DOWN       `0

#define WRAP_ENABLE      1
#define WRAP_DISABLE     0

/* Standard buffer size */
#define STD_BUF_SIZE     256

/* to distinguish between seek, tune during STC int. */
#define NO_SEEK_TUNE_PENDING 0
#define TUNE_PENDING 1
#define SEEK_PENDING 2
#define SCAN_PENDING 3

#define RTC6213N_MIN_SRCH_MODE 0x00
#define RTC6213N_MAX_SRCH_MODE 0x02

#define MIN_DWELL_TIME 0x00
#define MAX_DWELL_TIME 0x0F

#define TUNE_STEP_SIZE 10
#define NO_OF_RDS_BLKS 4

#define GET_MSB(x)((x >> 8) & 0xFF)
#define GET_LSB(x)((x) & 0xFF)

#define OFFSET_OF_GRP_TYP 11
#define RDS_INT_BIT 0x01
#define FIFO_CNT_16 0x10
#define UNCORRECTABLE_RDS_EN 0xFF01

/* Write starts with the upper byte of register 0x02 */
#define WRITE_REG_NUM       RADIO_REGISTER_NUM
#define WRITE_INDEX(i)      ((i + 0x02)%16)

/* Read starts with the upper byte of register 0x0a */
#define READ_REG_NUM        RADIO_REGISTER_NUM
#define READ_INDEX(i)       ((i + RADIO_REGISTER_NUM - 0x0a) % READ_REG_NUM)

#define MSB_OF_BLK_0 4
#define LSB_OF_BLK_0 5
#define MSB_OF_BLK_1 6
#define LSB_OF_BLK_1 7
#define MSB_OF_BLK_2 8
#define LSB_OF_BLK_2 9
#define MSB_OF_BLK_3 10
#define LSB_OF_BLK_3 11
#define MAX_RT_LEN 64
#define END_OF_RT 0x0d
#define MAX_PS_LEN 8
#define OFFSET_OF_PS 5
#define PS_VALIDATE_LIMIT 2
#define RT_VALIDATE_LIMIT 2
#define RDS_CMD_LEN 3
#define RDS_RSP_LEN 13
#define PS_EVT_DATA_LEN (MAX_PS_LEN + OFFSET_OF_PS)
#define NO_OF_PS 1
#define OFFSET_OF_RT 5
#define OFFSET_OF_PTY 5
#define MAX_LEN_2B_GRP_RT 32
#define CNT_FOR_2A_GRP_RT 4
#define CNT_FOR_2B_GRP_RT 2
#define PS_MASK 0x3
#define PTY_MASK 0x1F
#define NO_OF_CHARS_IN_EACH_ADD 2

#define CORRECTED_NONE          0
#define CORRECTED_ONE_TO_TWO    1
#define CORRECTED_THREE_TO_FIVE 2
#define ERRORS_CORRECTED(data,block) ((data>>block)&0x03)
/*Block Errors are reported in .5% increments*/
#define BLER_SCALE_MAX 200

/* freqs are divided by 10. */
#define SCALE_AF_CODE_TO_FREQ_KHZ(x) (87500 + (x*100))

#define RDS_TYPE_0A     (0 * 2 + 0)
#define RDS_TYPE_0B     (0 * 2 + 1)
#define RDS_TYPE_2A     (2 * 2 + 0)
#define RDS_TYPE_2B     (2 * 2 + 1)
#define RDS_TYPE_3A     (3 * 2 + 0)
#define UNCORRECTABLE           3

#define APP_GRP_typ_MASK	0x1F
/*ERT*/
#define ERT_AID			0x6552
#define MAX_ERT_SEGMENT		31
#define MAX_ERT_LEN		256
#define ERT_OFFSET		3
#define ERT_FORMAT_DIR_BIT	1
#define ERT_CNT_PER_BLK		2
/*RT PLUS*/
#define DUMMY_CLASS		0
#define RT_PLUS_LEN_1_TAG	3
#define RT_ERT_FLAG_BIT		13
#define RT_PLUS_AID             0x4bd7
#define RT_ERT_FLAG_OFFSET	1
#define RT_PLUS_OFFSET		2
/*TAG1*/
#define TAG1_MSB_OFFSET		3
#define TAG1_MSB_MASK		7
#define TAG1_LSB_OFFSET		13
#define TAG1_POS_MSB_MASK	0x3F
#define TAG1_POS_MSB_OFFSET	1
#define TAG1_POS_LSB_OFFSET	7
#define TAG1_LEN_OFFSET		1
#define TAG1_LEN_MASK		0x3F
/*TAG2*/
#define TAG2_MSB_OFFSET		5
#define TAG2_MSB_MASK		9
#define TAG2_LSB_OFFSET		11
#define TAG2_POS_MSB_MASK	0x3F
#define TAG2_POS_MSB_OFFSET	3
#define TAG2_POS_LSB_OFFSET	5
#define TAG2_LEN_MASK		0x1F

#define DEFAULT_AF_RSSI_LOW_TH 25
#define NO_OF_AF_IN_GRP 2
#define MAX_NO_OF_AF 25
#define MAX_AF_LIST_SIZE (MAX_NO_OF_AF * 4) /* 4 bytes per freq */
#define GET_AF_EVT_LEN(x) (7 + x*4)
#define GET_AF_LIST_LEN(x) (x*4)
#define MIN_AF_FREQ_CODE 1
#define MAX_AF_FREQ_CODE 204

/* 25 AFs supported for a freq. 224 means 1 AF. 225 means 2 AFs and so on */
#define NO_AF_CNT_CODE 224
#define MIN_AF_CNT_CODE 225
#define MAX_AF_CNT_CODE 249
#define AF_WAIT_SEC 10
#define MAX_AF_WAIT_SEC 255
#define AF_PI_WAIT_TIME 50 /* 50*100msec = 5sec */

#define CH_SPACING_200 200
#define CH_SPACING_100 100
#define CH_SPACING_50 50

#define RW_PRIBASE	(V4L2_CID_USER_BASE | 0xf000)

/* freqs are divided by 10. */
#define SCALE_AF_CODE_TO_FREQ_KHZ(x) (87500 + (x*100))

#define EXTRACT_BIT(data, bit_pos) ((data >> bit_pos) & 1)

#define V4L2_CID_PRIVATE_CSR0_ENABLE    (RW_PRIBASE + (DEVICEID<<4) + 1)
#define V4L2_CID_PRIVATE_CSR0_DISABLE   (RW_PRIBASE + (DEVICEID<<4) + 2)
#define V4L2_CID_PRIVATE_DEVICEID       (RW_PRIBASE + (DEVICEID<<4) + 3)

#define V4L2_CID_PRIVATE_CSR0_DIS_SMUTE (RW_PRIBASE + (DEVICEID<<4) + 4)
#define V4L2_CID_PRIVATE_CSR0_DIS_MUTE  (RW_PRIBASE + (DEVICEID<<4) + 5)
#define V4L2_CID_PRIVATE_CSR0_DEEM      (RW_PRIBASE + (DEVICEID<<4) + 6)
#define V4L2_CID_PRIVATE_CSR0_BLNDADJUST (RW_PRIBASE + (DEVICEID<<4) + 7)
#define V4L2_CID_PRIVATE_CSR0_VOLUME    (RW_PRIBASE + (DEVICEID<<4) + 8)

#define V4L2_CID_PRIVATE_CSR0_BAND      (RW_PRIBASE + (DEVICEID<<4) + 9)
#define V4L2_CID_PRIVATE_CSR0_CHSPACE   (RW_PRIBASE + (DEVICEID<<4) + 10)

#define V4L2_CID_PRIVATE_CSR0_DIS_AGC   (RW_PRIBASE + (DEVICEID<<4) + 11)
#define V4L2_CID_PRIVATE_CSR0_RDS_EN    (RW_PRIBASE + (DEVICEID<<4) + 12)

#define V4L2_CID_PRIVATE_SEEK_CANCEL    (RW_PRIBASE + (DEVICEID<<4) + 13)

#define V4L2_CID_PRIVATE_CSR0_SEEKRSSITH (RW_PRIBASE + (DEVICEID<<4) + 14)
#define V4L2_CID_PRIVATE_CSR0_OFSTH     (RW_PRIBASE + (DEVICEID<<4) + 15)
#define V4L2_CID_PRIVATE_CSR0_QLTTH     (RW_PRIBASE + (CHIPID<<4) + 0)
#define V4L2_CID_PRIVATE_RSSI           (RW_PRIBASE + (CHIPID<<4) + 1)

#define V4L2_CID_PRIVATE_RDS_RDY        (RW_PRIBASE + (CHIPID<<4) + 2)
#define V4L2_CID_PRIVATE_STD            (RW_PRIBASE + (CHIPID<<4) + 3)
#define V4L2_CID_PRIVATE_SF	            (RW_PRIBASE + (CHIPID<<4) + 4)
#define V4L2_CID_PRIVATE_RDS_SYNC	    (RW_PRIBASE + (CHIPID<<4) + 5)
#define V4L2_CID_PRIVATE_SI	            (RW_PRIBASE + (CHIPID<<4) + 6)

#define WAIT_OVER			0
#define SEEK_WAITING		1
#define NO_WAIT				2
#define TUNE_WAITING		4
#define RDS_WAITING			5
#define SEEK_CANCEL			6
/**************************************************************************
 * General Driver Definitions
 **************************************************************************/

enum rtc6213n_buf_t {
	RTC6213N_FM_BUF_SRCH_LIST,
	RTC6213N_FM_BUF_EVENTS,
	RTC6213N_FM_BUF_RT_RDS,
	RTC6213N_FM_BUF_PS_RDS,
	RTC6213N_FM_BUF_RAW_RDS,
	RTC6213N_FM_BUF_AF_LIST,
	RTC6213N_FM_BUF_RT_PLUS = 11,
	RTC6213N_FM_BUF_ERT,
	RTC6213N_FM_BUF_MAX
};

enum rtc6213n_evt_t {
	RTC6213N_EVT_RADIO_READY,
	RTC6213N_EVT_TUNE_SUCC,
	RTC6213N_EVT_SEEK_COMPLETE,
	RTC6213N_EVT_SCAN_NEXT,
	RTC6213N_EVT_NEW_RAW_RDS,
	RTC6213N_EVT_NEW_RT_RDS,
	RTC6213N_EVT_NEW_PS_RDS,
	RTC6213N_EVT_ERROR,
	RTC6213N_EVT_BELOW_TH,
	RTC6213N_EVT_ABOVE_TH,
	RTC6213N_EVT_STEREO,
	RTC6213N_EVT_MONO,
	RTC6213N_EVT_RDS_AVAIL,
	RTC6213N_EVT_RDS_NOT_AVAIL,
	RTC6213N_EVT_NEW_SRCH_LIST,
	RTC6213N_EVT_NEW_AF_LIST,
	RTC6213N_EVT_TXRDSDAT,
	RTC6213N_EVT_TXRDSDONE,
	RTC6213N_EVT_RADIO_DISABLED,
	RTC6213N_EVT_NEW_ODA,
	RTC6213N_EVT_NEW_RT_PLUS,
	RTC6213N_EVT_NEW_ERT
};

struct rtc6213n_recv_conf_req {
	__u16	emphasis;
	__u16	ch_spacing;
	/* limits stored as actual freq / TUNE_STEP_SIZE */
	__u16	band_low_limit;
	__u16	band_high_limit;
};

struct rtc6213n_rel_freq {
	__u8  rel_freq_msb;
	__u8  rel_freq_lsb;
} __packed;

struct rtc6213n_srch_list_compl {
	__u8    num_stations_found;
	struct rtc6213n_rel_freq  rel_freq[20];
} __packed;

struct af_list_ev {
	__le32   tune_freq_khz;
	__le16   pi_code;
	__u8    af_size;
	__u8    af_list[MAX_AF_LIST_SIZE];
} __packed;

struct rtc6213n_af_info {
	/* no. of invalid AFs. */
	u8 inval_freq_cnt;
	/* no. of AFs in the list. */
	u8 cnt;
	/* actual size of the list */
	u8 size;
	/* index of currently tuned station in the AF list. */
	u8 index;
	/* PI of the frequency */
	u16 pi;
	/* freq to which AF list belongs to. */
	u32 orig_freq_khz;
	/* AF list */
	u32 af_list[MAX_NO_OF_AF];
};

/*
 * rtc6213n_device - private data
 */
struct rtc6213n_device {
	int int_gpio;
	struct v4l2_device v4l2_dev;
	struct video_device videodev;
	struct pinctrl *fm_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
	struct v4l2_ctrl_handler ctrl_handler;
	int band;
	int space;
	unsigned int users;
	unsigned int mode;
	u8 seek_tune_status;
	/* Richwave internal registers (0..15) */
	unsigned short registers[RADIO_REGISTER_NUM];

	/* RDS receive buffer */
	wait_queue_head_t read_queue;
	int irq;
	int tuned_freq_khz;
	int dwell_time_sec;
	struct mutex lock;      /* buffer locking */
	unsigned char *buffer;      /* size is always multiple of three */
	bool is_search_cancelled;
	u8 g_search_mode;
	struct rtc6213n_srch_list_compl srch_list;
	/* buffer locks*/
	spinlock_t buf_lock[RTC6213N_FM_BUF_MAX];
	struct rtc6213n_recv_conf_req recv_conf;
	struct workqueue_struct *wqueue;
	struct workqueue_struct *wqueue_scan;
	struct workqueue_struct *wqueue_rds;
	struct work_struct rds_worker;
	struct rtc6213n_af_info af_info1;
	struct rtc6213n_af_info af_info2;

	struct delayed_work work;
	struct delayed_work work_scan;

	wait_queue_head_t event_queue;
	u8 write_buf[WRITE_REG_NUM];
	/* TO read events, data*/
	u8 read_buf[READ_REG_NUM];

	u16 pi; /* PI of tuned channel */
	u8 pty; /* programe type of the tuned channel */

	u16 block[NO_OF_RDS_BLKS];
	u8 rt_display[MAX_RT_LEN];   /* RT that will be displayed */
	u8 rt_tmp0[MAX_RT_LEN]; /* high probability RT */
	u8 rt_tmp1[MAX_RT_LEN]; /* low probability RT */
	u8 rt_cnt[MAX_RT_LEN];  /* high probability RT's hit count */
	u8 rt_flag;          /* A/B flag of RT */
	bool valid_rt_flg;     /* validity of A/B flag */
	u8 ps_display[MAX_PS_LEN];    /* PS that will be displayed */
	u8 ps_tmp0[MAX_PS_LEN]; /* high probability PS */
	u8 ps_tmp1[MAX_PS_LEN]; /* low probability PS */
	u8 ps_cnt[MAX_PS_LEN];  /* high probability PS's hit count */
	u8 bler[NO_OF_RDS_BLKS];
	u8 rt_plus_carrier;
	u8 ert_carrier;
	u8 ert_buf[MAX_ERT_LEN];
	u8 ert_len;
	u8 c_byt_pair_index;
	u8 utf_8_flag;
	u8 rt_ert_flag;
	u8 formatting_dir;
	unsigned int buf_size;
	unsigned int rd_index;
	unsigned int wr_index;

	struct kfifo data_buf[RTC6213N_FM_BUF_MAX];

	struct completion completion;
	bool stci_enabled;      /* Seek/Tune Complete Interrupt */

	struct i2c_client *client;
	unsigned int tuner_state;
};

enum radio_state_t {
	FM_OFF,
	FM_RECV,
	FM_RESET,
	FM_CALIB,
	FM_TURNING_OFF,
	FM_RECV_TURNING_ON,
	FM_MAX_NO_STATES,
};

enum search_t {
	SEEK,
	SCAN,
	SCAN_FOR_STRONG,
};

/**************************************************************************
 * Frequency Multiplicator
 **************************************************************************/
#define FREQ_MUL 1000
#define CONFIG_RDS

enum v4l2_cid_private_rtc6213n_t {
	V4L2_CID_PRIVATE_RTC6213N_SRCHMODE = (V4L2_CID_PRIVATE_BASE + 1),
	V4L2_CID_PRIVATE_RTC6213N_SCANDWELL,
	V4L2_CID_PRIVATE_RTC6213N_SRCHON,
	V4L2_CID_PRIVATE_RTC6213N_STATE,
	V4L2_CID_PRIVATE_RTC6213N_TRANSMIT_MODE,
	V4L2_CID_PRIVATE_RTC6213N_RDSGROUP_MASK,
	V4L2_CID_PRIVATE_RTC6213N_REGION,
	V4L2_CID_PRIVATE_RTC6213N_SIGNAL_TH,
	V4L2_CID_PRIVATE_RTC6213N_SRCH_PTY,
	V4L2_CID_PRIVATE_RTC6213N_SRCH_PI,
	V4L2_CID_PRIVATE_RTC6213N_SRCH_CNT,
	V4L2_CID_PRIVATE_RTC6213N_EMPHASIS,	/* 800000c */
	V4L2_CID_PRIVATE_RTC6213N_RDS_STD,
	V4L2_CID_PRIVATE_RTC6213N_SPACING,
	V4L2_CID_PRIVATE_RTC6213N_RDSON,
	V4L2_CID_PRIVATE_RTC6213N_RDSGROUP_PROC,
	V4L2_CID_PRIVATE_RTC6213N_LP_MODE,
	V4L2_CID_PRIVATE_RTC6213N_ANTENNA,
	V4L2_CID_PRIVATE_RTC6213N_RDSD_BUF,
	V4L2_CID_PRIVATE_RTC6213N_PSALL,
	/*v4l2 Tx controls*/
	V4L2_CID_PRIVATE_RTC6213N_TX_SETPSREPEATCOUNT,
	V4L2_CID_PRIVATE_RTC6213N_STOP_RDS_TX_PS_NAME,
	V4L2_CID_PRIVATE_RTC6213N_STOP_RDS_TX_RT,
	V4L2_CID_PRIVATE_RTC6213N_IOVERC,
	V4L2_CID_PRIVATE_RTC6213N_INTDET,
	V4L2_CID_PRIVATE_RTC6213N_MPX_DCC,
	V4L2_CID_PRIVATE_RTC6213N_AF_JUMP,
	V4L2_CID_PRIVATE_RTC6213N_RSSI_DELTA,
	V4L2_CID_PRIVATE_RTC6213N_HLSI,

	/*
	* Here we have IOCTl's that are specific to IRIS
	* (V4L2_CID_PRIVATE_BASE + 0x1E to V4L2_CID_PRIVATE_BASE + 0x28)
	*/
	V4L2_CID_PRIVATE_RTC6213N_SOFT_MUTE,/* 0x800001E*/
	V4L2_CID_PRIVATE_RTC6213N_RIVA_ACCS_ADDR,
	V4L2_CID_PRIVATE_RTC6213N_RIVA_ACCS_LEN,
	V4L2_CID_PRIVATE_RTC6213N_RIVA_PEEK,
	V4L2_CID_PRIVATE_RTC6213N_RIVA_POKE,
	V4L2_CID_PRIVATE_RTC6213N_SSBI_ACCS_ADDR,
	V4L2_CID_PRIVATE_RTC6213N_SSBI_PEEK,
	V4L2_CID_PRIVATE_RTC6213N_SSBI_POKE,
	V4L2_CID_PRIVATE_RTC6213N_TX_TONE,
	V4L2_CID_PRIVATE_RTC6213N_RDS_GRP_COUNTERS,
	V4L2_CID_PRIVATE_RTC6213N_SET_NOTCH_FILTER,/* 0x8000028 */

	V4L2_CID_PRIVATE_RTC6213N_SET_AUDIO_PATH,/* 0x8000029 */
	V4L2_CID_PRIVATE_RTC6213N_DO_CALIBRATION,/* 0x800002A : IRIS */
	V4L2_CID_PRIVATE_RTC6213N_SRCH_ALGORITHM,/* 0x800002B */
	V4L2_CID_PRIVATE_RTC6213N_GET_SINR, /* 0x800002C : IRIS */
	V4L2_CID_PRIVATE_RTC6213N_INTF_LOW_THRESHOLD, /* 0x800002D */
	V4L2_CID_PRIVATE_RTC6213N_INTF_HIGH_THRESHOLD, /* 0x800002E */
	V4L2_CID_PRIVATE_RTC6213N_SINR_THRESHOLD,  /* 0x800002F : IRIS */
	V4L2_CID_PRIVATE_RTC6213N_SINR_SAMPLES,  /* 0x8000030 : IRIS */
	V4L2_CID_PRIVATE_RTC6213N_SPUR_FREQ,
	V4L2_CID_PRIVATE_RTC6213N_SPUR_FREQ_RMSSI,
	V4L2_CID_PRIVATE_RTC6213N_SPUR_SELECTION,
	V4L2_CID_PRIVATE_RTC6213N_UPDATE_SPUR_TABLE,
	V4L2_CID_PRIVATE_RTC6213N_VALID_CHANNEL,
	V4L2_CID_PRIVATE_RTC6213N_AF_RMSSI_TH,
	V4L2_CID_PRIVATE_RTC6213N_AF_RMSSI_SAMPLES,
	V4L2_CID_PRIVATE_RTC6213N_GOOD_CH_RMSSI_TH,
	V4L2_CID_PRIVATE_RTC6213N_SRCHALGOTYPE,
	V4L2_CID_PRIVATE_RTC6213N_CF0TH12,
	V4L2_CID_PRIVATE_RTC6213N_SINRFIRSTSTAGE,
	V4L2_CID_PRIVATE_RTC6213N_RMSSIFIRSTSTAGE,
	V4L2_CID_PRIVATE_RTC6213N_RXREPEATCOUNT,
	V4L2_CID_PRIVATE_RTC6213N_RSSI_TH, /* 0x800003E */
	V4L2_CID_PRIVATE_RTC6213N_AF_JUMP_RSSI_TH /* 0x800003F */

};

/**************************************************************************
 * Common Functions
 **************************************************************************/
extern struct i2c_driver rtc6213n_i2c_driver;
extern struct video_device rtc6213n_viddev_template;
extern const struct v4l2_ioctl_ops rtc6213n_ioctl_ops;
extern const struct v4l2_ctrl_ops rtc6213n_ctrl_ops;

extern struct tasklet_struct my_tasklet;
extern int rtc6213n_wq_flag;
extern wait_queue_head_t rtc6213n_wq;
extern int rtc6213n_get_all_registers(struct rtc6213n_device *radio);
extern int rtc6213n_get_register(struct rtc6213n_device *radio, int regnr);
extern int rtc6213n_set_register(struct rtc6213n_device *radio, int regnr);
extern int rtc6213n_set_serial_registers(struct rtc6213n_device *radio,
	u16 *data, int bytes);
int rtc6213n_i2c_init(void);
int rtc6213n_reset_rds_data(struct rtc6213n_device *radio);
int rtc6213n_set_freq(struct rtc6213n_device *radio, unsigned int freq);
int rtc6213n_start(struct rtc6213n_device *radio);
int rtc6213n_stop(struct rtc6213n_device *radio);
int rtc6213n_fops_open(struct file *file);
int rtc6213n_power_up(struct rtc6213n_device *radio);
int rtc6213n_power_down(struct rtc6213n_device *radio);
int rtc6213n_fops_release(struct file *file);
int rtc6213n_vidioc_querycap(struct file *file, void *priv,
	struct v4l2_capability *capability);
int rtc6213n_enable_irq(struct rtc6213n_device *radio);
void rtc6213n_disable_irq(struct rtc6213n_device *radio);
void rtc6213n_scan(struct work_struct *work);
void rtc6213n_search(struct rtc6213n_device *radio, bool on);
int rtc6213n_cancel_seek(struct rtc6213n_device *radio);
void rtc6213n_rds_handler(struct work_struct *worker);
