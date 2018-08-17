/*
 *  drivers/media/radio/rtc6213n/radio-rtc6213n-common.c
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


/*
 * History:
 * 2013-05-12   TianTsai Chang <changtt@richwave.com.tw>
 *      Version 1.0.0
 *      - First working version
 */

/* kernel includes */
#include <linux/delay.h>
#include <linux/i2c.h>
#include "radio-rtc6213n.h"
#define New_VolumeControl
/**************************************************************************
 * Module Parameters
 **************************************************************************/

/* Spacing (kHz) */
/* 0: 200 kHz (USA, Australia) */
/* 1: 100 kHz (Europe, Japan) */
/* 2:  50 kHz */
static unsigned short space = 2;
module_param(space, ushort, 0444);
MODULE_PARM_DESC(space, "Spacing: 0=200kHz *1=100kHz* 2=50kHz");

/* Bottom of Band (MHz) */
/* 0: 87.5 - 108 MHz (USA, Europe)*/
/* 1: 76   - 108 MHz (Japan wide band) */
/* 2: 76   -  90 MHz (Japan) */
static unsigned short band = 0;
module_param(band, ushort, 0444);
MODULE_PARM_DESC(band, "Band: *0=87.5-108MHz* 1=76-108MHz 2=76-91MHz 3=65-76MHz");

/* De-emphasis */
/* 0: 75 us (USA) */
/* 1: 50 us (Europe, Australia, Japan) */
static unsigned short de;
module_param(de, ushort, 0444);
MODULE_PARM_DESC(de, "De-emphasis: *0=75us* 1=50us");

/* Tune timeout */
static unsigned int tune_timeout = 3000;
module_param(tune_timeout, uint, 0644);
MODULE_PARM_DESC(tune_timeout, "Tune timeout: *3000*");

/* Seek timeout */
static unsigned int seek_timeout = 8000;
module_param(seek_timeout, uint, 0644);
MODULE_PARM_DESC(seek_timeout, "Seek timeout: *8000*");

static const struct v4l2_frequency_band bands[] = {
	{
		.type = V4L2_TUNER_RADIO,
		.index = 0,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  87500,
		.rangehigh  = 108000,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 1,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  76000,
		.rangehigh  = 108000,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 2,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  76000,
		.rangehigh  =  91000,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
	{
		.type = V4L2_TUNER_RADIO,
		.index = 3,
		.capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
			    V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO |
			    V4L2_TUNER_CAP_FREQ_BANDS |
			    V4L2_TUNER_CAP_HWSEEK_BOUNDED |
			    V4L2_TUNER_CAP_HWSEEK_WRAP,
		.rangelow   =  64000,
		.rangehigh  =  76000,
		.modulation = V4L2_BAND_MODULATION_FM,
	},
};

wait_queue_head_t rtc6213n_wq;
int rtc6213n_wq_flag = NO_WAIT;
#ifdef New_VolumeControl
unsigned short global_volume;
#endif
void rtc6213n_q_event(struct rtc6213n_device *radio,
		enum rtc6213n_evt_t event)
{

	struct kfifo *data_b;
	unsigned char evt = event;

	data_b = &radio->data_buf[RTC6213N_FM_BUF_EVENTS];

	pr_info("%s updating event_q with event %x\n", __func__, event);
	if (kfifo_in_locked(data_b,
				&evt,
				1,
				&radio->buf_lock[RTC6213N_FM_BUF_EVENTS]))
		wake_up_interruptible(&radio->event_queue);
}

/*
 * rtc6213n_set_chan - set the channel
 */
static int rtc6213n_set_chan(struct rtc6213n_device *radio, unsigned short chan)
{
	int retval;
	bool timed_out = 0;
	unsigned short current_chan =
		radio->registers[CHANNEL] & CHANNEL_CSR0_CH;

	pr_info("%s CHAN=%d chan=%d\n",__func__, radio->registers[CHANNEL], chan);

	/* start tuning */
	radio->registers[CHANNEL] &= ~CHANNEL_CSR0_CH;
	radio->registers[CHANNEL] |= CHANNEL_CSR0_TUNE | chan;
	retval = rtc6213n_set_register(radio, CHANNEL);
	if (retval < 0)	{
		radio->registers[CHANNEL] = current_chan;
		goto done;
	}
	reinit_completion(&radio->completion);
	retval = wait_for_completion_timeout(&radio->completion,
			msecs_to_jiffies(tune_timeout));
	if (!retval)
		timed_out = true;

	if ((radio->registers[STATUS] & STATUS_STD) == 0)
		pr_info("%s tune does not complete\n", __func__);
	else {
		radio->seek_tune_status = TUNE_PENDING;
		rtc6213n_q_event(radio, RTC6213N_EVT_TUNE_SUCC);
	}
	if (timed_out)
		pr_info("%s tune timed out after %u ms\n", __func__ ,tune_timeout);

	/* stop tuning */
	current_chan = radio->registers[CHANNEL] & CHANNEL_CSR0_CH;

	radio->registers[CHANNEL] &= ~CHANNEL_CSR0_TUNE;
	retval = rtc6213n_set_register(radio, CHANNEL);
	if (retval < 0)	{
		radio->registers[CHANNEL] = current_chan;
		goto done;
	}

	done:
	pr_info("%s exit %d\n", __func__, retval);
	return retval;
}


/*
 * rtc6213n_get_freq - get the frequency
 */
static int rtc6213n_get_freq(struct rtc6213n_device *radio, unsigned int *freq)
{
	unsigned int spacing, band_bottom, temp_freq;
	unsigned short chan;
	int retval;

	pr_info("%s enter\n", __func__);
	/* Spacing (kHz) */
	switch ((radio->registers[CHANNEL] & CHANNEL_CSR0_CHSPACE) >> 10) {
	/* 0: 200 kHz (USA, Australia) */
	case 0:
		spacing = 0.200 * FREQ_MUL; break;
	/* 1: 100 kHz (Europe, Japan) */
	case 1:
		spacing = 0.100 * FREQ_MUL; break;
	/* 2:  50 kHz */
	default:
		spacing = 0.050 * FREQ_MUL; break;
	};

	/* Bottom of Band (MHz) */
	switch ((radio->registers[CHANNEL] & CHANNEL_CSR0_BAND) >> 12) {
	/* 0: 87.5 - 108 MHz (USA, Europe) */
	case 0:
		band_bottom = 87.5 * FREQ_MUL; break;
	/* 1: 76   - 108 MHz (Japan wide band) */
	default:
		band_bottom = 76   * FREQ_MUL; break;
	/* 2: 76   -  90 MHz (Japan) */
	case 2:
		band_bottom = 76   * FREQ_MUL; break;
	};

	/* read channel */
	retval = rtc6213n_get_register(radio, STATUS);
	if (retval < 0){
		pr_info("%s fail to get register\n", __func__);
		goto end;
	}
	chan = radio->registers[STATUS] & STATUS_READCH;

	pr_info("%s chan %d\n", __func__, chan);
	/* Frequency (MHz) = Spacing (kHz) x Channel + Bottom of Band (MHz) */
	temp_freq = chan * spacing + band_bottom;
	*freq = temp_freq * 16;
	pr_info("%s tempfreq=%d, freq=%d\n",__func__, temp_freq, *freq);

end:
	return retval;
}


/*
 * rtc6213n_set_freq - set the frequency
 */
int rtc6213n_set_freq(struct rtc6213n_device *radio, unsigned int freq)
{
	unsigned int spacing, band_bottom;
	unsigned short chan;

	freq = freq / 16;
	pr_info("%s enter freq:%d\n", __func__, freq);
	/* Spacing (kHz) */
	switch ((radio->registers[CHANNEL] & CHANNEL_CSR0_CHSPACE) >> 10) {
	/* 0: 200 kHz (USA, Australia) */
	case 0:
		spacing = 0.200 * FREQ_MUL; break;
	/* 1: 100 kHz (Europe, Japan) */
	case 1:
		spacing = 0.100 * FREQ_MUL; break;
	/* 2:  50 kHz */
	default:
		spacing = 0.050 * FREQ_MUL; break;
	};

	/* Bottom of Band (MHz) */
	switch ((radio->registers[CHANNEL] & CHANNEL_CSR0_BAND) >> 12) {
	/* 0: 87.5 - 108 MHz (USA, Europe) */
	case 0:
		band_bottom = 87.5 * FREQ_MUL; break;
	/* 1: 76   - 108 MHz (Japan wide band) */
	default:
		band_bottom = 76   * FREQ_MUL; break;
	/* 2: 76   -  90 MHz (Japan) */
	case 2:
		band_bottom = 76   * FREQ_MUL; break;
	};

	if (freq < band_bottom)
		freq = band_bottom;

	/* Chan = [ Freq (Mhz) - Bottom of Band (MHz) ] / Spacing (kHz) */
	chan = (freq - band_bottom) / spacing;

	pr_info("%s chan:%d freq:%d  band_bottom:%d  spacing:%d\n", __func__,
			chan, freq, band_bottom, spacing);
	return rtc6213n_set_chan(radio, chan);
}


/*
 * rtc6213n_set_seek - set seek
 */
static int rtc6213n_set_seek(struct rtc6213n_device *radio,
	 unsigned int seek_up, unsigned int seek_wrap)
{
	int retval = 0;
	bool timed_out = 0;
	unsigned short seekcfg1_val = radio->registers[SEEKCFG1];

	pr_info("%s enter up:%d wrap:%d\n", __func__, seek_up, seek_wrap);
	if (seek_wrap)
		radio->registers[SEEKCFG1] &= ~SEEKCFG1_CSR0_SKMODE;
	else
		radio->registers[SEEKCFG1] |= SEEKCFG1_CSR0_SKMODE;

	if (seek_up)
		radio->registers[SEEKCFG1] |= SEEKCFG1_CSR0_SEEKUP;
	else
		radio->registers[SEEKCFG1] &= ~SEEKCFG1_CSR0_SEEKUP;

	retval = rtc6213n_set_register(radio, SEEKCFG1);
	if (retval < 0) {
		radio->registers[SEEKCFG1] = seekcfg1_val;
		goto done;
	}

	/* start seeking */
	radio->registers[SEEKCFG1] |= SEEKCFG1_CSR0_SEEK;
	retval = rtc6213n_set_register(radio, SEEKCFG1);
	if (retval < 0) {
		radio->registers[SEEKCFG1] = seekcfg1_val;
		goto done;
	}

	reinit_completion(&radio->completion);
	retval = wait_for_completion_timeout(&radio->completion,
			msecs_to_jiffies(seek_timeout));
	if (!retval){
		timed_out = true;
		pr_err("%s timeout\n",__func__);
	}

	if ((radio->registers[STATUS] & STATUS_STD) == 0)
		pr_info(" %s seek does not complete\n", __func__);
	if (timed_out){
		pr_info(" %s seek timed out\n",__func__);
		retval = -EAGAIN;
	}
	/* stop seeking : clear STD*/
	radio->registers[SEEKCFG1] &= ~SEEKCFG1_CSR0_SEEK;
	retval = rtc6213n_set_register(radio, SEEKCFG1);

done:
	if (radio->registers[STATUS] & STATUS_SF) {
		pr_info(" %s seek failed / band limit reached\n", __func__);
		retval = -ESPIPE;
	}
	pr_info("%s exit %d\n", __func__, retval);
	return retval;
}

static void rtc6213n_update_search_list(struct rtc6213n_device *radio, int freq)
{
	int temp_freq = freq;

	temp_freq = temp_freq -
		(radio->recv_conf.band_low_limit * TUNE_STEP_SIZE);
	temp_freq = temp_freq / 50;
	radio->srch_list.rel_freq[radio->srch_list.num_stations_found].
		rel_freq_lsb = GET_LSB(temp_freq);
	radio->srch_list.rel_freq[radio->srch_list.num_stations_found].
		rel_freq_msb = GET_MSB(temp_freq);
	radio->srch_list.num_stations_found++;
}

void rtc6213n_scan(struct work_struct *work)
{
	struct rtc6213n_device *radio;
	int current_freq_khz;
	struct kfifo *data_b;
	int len = 0;
	u32 temp_freq_khz;
	u32 list_khz;
	int retval = 0;

	pr_info("%s enter\n", __func__);

	radio = container_of(work, struct rtc6213n_device, work_scan.work);
	radio->seek_tune_status = SEEK_PENDING;

	retval = rtc6213n_get_freq(radio, &current_freq_khz);
	if(retval < 0){
		pr_err("%s fail to get freq\n",__func__);
		goto seek_tune_fail;
	}
	pr_info("%s cuurent freq %d\n", __func__, current_freq_khz);

	while(1) {
		if (radio->is_search_cancelled == true) {
			pr_err("%s: scan cancelled\n", __func__);
			if (radio->g_search_mode == SCAN_FOR_STRONG)
				goto seek_tune_fail;
			else
				goto seek_cancelled;
			goto seek_cancelled;
		} else if (radio->mode != FM_RECV) {
			pr_err("%s: FM is not in proper state\n", __func__);
			return ;
		}

		retval = rtc6213n_set_seek(radio, SRCH_UP, WRAP_DISABLE);
		if (retval < 0) {
			pr_err("%s seek fail %d\n", __func__, retval);
			goto seek_tune_fail;
		}

		retval = rtc6213n_get_freq(radio, &temp_freq_khz);
		if(retval < 0){
			pr_err("%s fail to get freq\n",__func__);
			goto seek_tune_fail;
		}
		pr_info("%s next freq %d\n", __func__, temp_freq_khz);

		if (radio->registers[STATUS] & STATUS_SF) {
			pr_err("%s seek failed / band limit reached\n",__func__);
			rtc6213n_q_event(radio, RTC6213N_EVT_TUNE_SUCC);
			break;
		}

		if (radio->g_search_mode == SCAN)
			rtc6213n_q_event(radio, RTC6213N_EVT_TUNE_SUCC);

		if (radio->is_search_cancelled == true) {
			pr_err("%s: scan cancelled\n", __func__);
			if (radio->g_search_mode == SCAN_FOR_STRONG)
				goto seek_tune_fail;
			else
				goto seek_cancelled;
			goto seek_cancelled;
		} else if (radio->mode != FM_RECV) {
			pr_err("%s: FM is not in proper state\n", __func__);
			return ;
		}

		if (radio->g_search_mode == SCAN) {
			/* sleep for dwell period */
			msleep(radio->dwell_time_sec * 1000);
			/* need to queue the event when the seek completes */
			rtc6213n_q_event(radio, RTC6213N_EVT_SCAN_NEXT);
		} else if (radio->g_search_mode == SCAN_FOR_STRONG) {
			list_khz = temp_freq_khz*16;
			rtc6213n_update_search_list(radio, list_khz);
		}

	}

seek_tune_fail:
	if (radio->g_search_mode == SCAN_FOR_STRONG) {
		len = radio->srch_list.num_stations_found * 2 +
			sizeof(radio->srch_list.num_stations_found);
		data_b = &radio->data_buf[RTC6213N_FM_BUF_SRCH_LIST];
		kfifo_in_locked(data_b, &radio->srch_list, len,
				&radio->buf_lock[RTC6213N_FM_BUF_SRCH_LIST]);
		rtc6213n_q_event(radio, RTC6213N_EVT_NEW_SRCH_LIST);
	}
	/* tune to original frequency */
	retval = rtc6213n_set_freq(radio, current_freq_khz);
	if (retval < 0)
		pr_err("%s: Tune to orig freq failed with error %d\n",
				__func__, retval);

	pr_err("%s seek tune fail %d",__func__, retval);

seek_cancelled:
	rtc6213n_q_event(radio, RTC6213N_EVT_SEEK_COMPLETE);
	radio->seek_tune_status = NO_SEEK_TUNE_PENDING;
	pr_err("%s seek cancelled %d",__func__, retval);
	return ;

}

int rtc6213n_cancel_seek(struct rtc6213n_device *radio)
{
	int retval = 0;

	pr_info("%s enter\n",__func__);
	mutex_lock(&radio->lock);

	/* stop seeking */
	radio->registers[SEEKCFG1] &= ~SEEKCFG1_CSR0_SEEK;
	retval = rtc6213n_set_register(radio, SEEKCFG1);

	mutex_unlock(&radio->lock);
	radio->is_search_cancelled = true;

	return retval;

}

void rtc6213n_search(struct rtc6213n_device *radio, bool on)
{
	int current_freq_khz;

	current_freq_khz = radio->tuned_freq_khz;

	if (on) {
		pr_info("%s: Queuing the work onto scan work q\n", __func__);
		queue_delayed_work(radio->wqueue_scan, &radio->work_scan,
					msecs_to_jiffies(10));
	} else {
		rtc6213n_cancel_seek(radio);
		rtc6213n_q_event(radio, RTC6213N_EVT_SEEK_COMPLETE);
	}
}

/*
 * rtc6213n_start - switch on radio
 */
int rtc6213n_start(struct rtc6213n_device *radio)
{
	int retval;
	u8 i2c_error;
	u16 swbk1[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7000, 0x4000,
		0x1A08, 0x0100, 0x0740, 0x0040, 0x005A, 0x02C0, 0x0000,
		0x1440, 0x0080, 0x0840, 0x0000, 0x4002, 0x805A, 0x0D35,
		0x7367, 0x0000};
	u16 swbk2[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7000, 0x8000,
		0x0000, 0x0000, 0x0333, 0x051C, 0x01EB, 0x01EB, 0x0333,
		0xF2AB, 0x7F8A, 0x0780, 0x0000, 0x1400, 0x405A, 0x0000,
		0x3200, 0x0000};
    /* New Added Register */
	u16 swbk3[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7000, 0xC000,
		0x188F, 0x9628, 0x4040, 0x80FF, 0xCFB0, 0x06F6, 0x0D40,
		0x0998, 0xC61F, 0x7126, 0x3F4B, 0xEED7, 0xB599, 0x674E,
		0x3112, 0x0000};
	u16 swbk4[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7000, 0x2000,
		0x050F, 0x0E85, 0x5AA6, 0xDC57, 0x8000, 0x00A3, 0x00A3,
		0xC018, 0x7F80, 0x3C08, 0xB6CF, 0x8100, 0x0000, 0x0140,
		0x4700, 0x0000};
	u16 swbk5[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7000, 0x6000,
		0x3590, 0x6311, 0x3008, 0x0019, 0x0D79, 0x7D2F, 0x8000,
		0x02A1, 0x771F, 0x323E, 0x262E, 0xA516, 0x8680, 0x0000,
		0x0000, 0x0000};
	u16 swbk7[] = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x7000, 0xE000,
		0x11A2, 0x0F92, 0x0000, 0x0000, 0x0000, 0x0000, 0x801D,
		0x0000, 0x0000, 0x0072, 0x00FF, 0x001F, 0x03FF, 0x16D1,
		0x13B7, 0x0000};

	radio->registers[BANKCFG] = 0x0000;

	i2c_error = 0;
	/* Keep in case of any unpredicted control */
	/* Set 0x16AA */
	radio->registers[DEVICEID] = 0x16AA;
	/* released the I2C from unexpected I2C start condition */
	retval = rtc6213n_set_register(radio, DEVICEID);
	/* recheck TH : 10 */
	while ((retval < 0) && (i2c_error < 10)) {
		retval = rtc6213n_set_register(radio, DEVICEID);
		i2c_error++;
	}

	if (retval < 0)	{
		pr_err("%s set to fail retval = %d\n",__func__,	retval);
		/* goto done;*/
	}
	msleep(30);

	/* Don't read all between writing 0x16AA and 0x96AA */
	i2c_error = 0;
	radio->registers[DEVICEID] = 0x96AA;
	retval = rtc6213n_set_register(radio, DEVICEID);
	/* recheck TH : 10 */
	while ((retval < 0) && (i2c_error < 10)) {
		retval = rtc6213n_set_register(radio, DEVICEID);
		i2c_error++;
	}

	if (retval < 0) {
		pr_err("%s set to fail 0x96AA %d\n",__func__, retval);
	}
	msleep(30);

	rtc6213n_get_register(radio, DEVICEID);
	rtc6213n_get_register(radio, CHIPID);

	pr_info("%s DeviceID=0x%x ChipID=0x%x Addr=0x%x\n", __func__,
		radio->registers[DEVICEID], radio->registers[CHIPID],
		radio->client->addr);

	retval = rtc6213n_set_serial_registers(radio, swbk1, 23);
	if (retval < 0)
		goto done;

	retval = rtc6213n_set_serial_registers(radio, swbk2, 23);
	if (retval < 0)
		goto done;

    /* New Added Register */
	retval = rtc6213n_set_serial_registers(radio, swbk3, 23);
	if (retval < 0)
		goto done;

	retval = rtc6213n_set_serial_registers(radio, swbk4, 23);
	if (retval < 0)
		goto done;

	retval = rtc6213n_set_serial_registers(radio, swbk5, 23);
	if (retval < 0)
		goto done;

	retval = rtc6213n_set_serial_registers(radio, swbk7, 23);
	if (retval < 0)
		goto done;

done:
	return retval;
}

/*
 * rtc6213n_stop - switch off radio
 */
int rtc6213n_stop(struct rtc6213n_device *radio)
{
	int retval;

	/* sysconfig */
	radio->registers[SYSCFG] &= ~SYSCFG_CSR0_RDS_EN;
	radio->registers[SYSCFG] &= ~SYSCFG_CSR0_RDSIRQEN;
	radio->registers[SYSCFG] &= ~SYSCFG_CSR0_STDIRQEN;
	retval = rtc6213n_set_register(radio, SYSCFG);
	if (retval < 0)
		goto done;

	/* powerconfig */
	radio->registers[MPXCFG] &= ~MPXCFG_CSR0_DIS_MUTE;
	retval = rtc6213n_set_register(radio, MPXCFG);

	/* POWERCFG_ENABLE has to automatically go low */
	radio->registers[POWERCFG] |= POWERCFG_CSR0_DISABLE;
	radio->registers[POWERCFG] &= ~POWERCFG_CSR0_ENABLE;
	retval = rtc6213n_set_register(radio, POWERCFG);

	/* Set 0x16AA */
	radio->registers[DEVICEID] = 0x16AA;
	retval = rtc6213n_set_register(radio, DEVICEID);

done:
	return retval;
}

static void rtc6213n_get_rds(struct rtc6213n_device *radio)
{
	int retval = 0;
	int i;

	mutex_lock(&radio->lock);
	retval = rtc6213n_get_all_registers(radio);

	if (retval < 0) {
		pr_err("%s read fail%d\n",__func__, retval);
		mutex_unlock(&radio->lock);
		return;
	}
	radio->block[0] = radio->registers[BA_DATA];
	radio->block[1] = radio->registers[BB_DATA];
	radio->block[2] = radio->registers[BC_DATA];
	radio->block[3] = radio->registers[BD_DATA];

	for(i = 0; i < 4; i++)
		pr_info("%s block[%d] %x \n", __func__, i, radio->block[i]);

	radio->bler[0] = (radio->registers[RSSI] & RDS_BA_ERRS) >> 14;
	radio->bler[1] = (radio->registers[RSSI] & RDS_BB_ERRS) >> 12;
	radio->bler[2] = (radio->registers[RSSI] & RDS_BC_ERRS) >> 10;
	radio->bler[3] = (radio->registers[RSSI] & RDS_BD_ERRS) >> 8;
	mutex_unlock(&radio->lock);
}

static void rtc6213n_pi_check(struct rtc6213n_device *radio, u16 current_pi)
{
	if (radio->pi != current_pi) {
		pr_info("%s current_pi %x , radio->pi %x\n"
				, __func__, current_pi, radio->pi);
		radio->pi = current_pi;
	} else {
		pr_info("%s Received same PI code\n",__func__);
	}
}

static void rtc6213n_pty_check(struct rtc6213n_device *radio, u8 current_pty)
{
	if (radio->pty != current_pty) {
		pr_info("%s PTY code of radio->block[1] = %x\n", __func__, current_pty);
		radio->pty = current_pty;
	} else {
		pr_info("%s PTY repeated\n",__func__);
	}
}

static bool is_new_freq(struct rtc6213n_device *radio, u32 freq)
{
	u8 i = 0;

	for (i = 0; i < radio->af_info2.size; i++) {
		if (freq == radio->af_info2.af_list[i])
			return false;
	}

	return true;
}

static bool is_different_af_list(struct rtc6213n_device *radio)
{
	u8 i = 0, j = 0;
	u32 freq;

	if (radio->af_info1.orig_freq_khz != radio->af_info2.orig_freq_khz)
		return true;

	/* freq is same, check if the AFs are same. */
	for (i = 0; i < radio->af_info1.size; i++) {
		freq = radio->af_info1.af_list[i];
		for (j = 0; j < radio->af_info2.size; j++) {
			if (freq == radio->af_info2.af_list[j])
				break;
		}

		/* freq is not there in list2 i.e list1, list2 are different.*/
		if (j == radio->af_info2.size)
			return true;
	}

	return false;
}

static bool is_valid_freq(struct rtc6213n_device *radio, u32 freq)
{
	u32 band_low_limit;
	u32 band_high_limit;
	u8 spacing = 0;

	band_low_limit = bands[radio->band].rangelow;
	band_high_limit = bands[radio->band].rangehigh;

	if (radio->space == 0)
		spacing = CH_SPACING_200;
	else if (radio->space == 1)
		spacing = CH_SPACING_100;
	else if (radio->space == 2)
		spacing = CH_SPACING_50;
	else
		return false;

	if ((freq >= band_low_limit) &&
			(freq <= band_high_limit) &&
			((freq - band_low_limit) % spacing == 0))
		return true;

	return false;
}

static void rtc6213n_update_af_list(struct rtc6213n_device *radio)
{

	bool retval;
	u8 i = 0;
	u8 af_data = radio->block[2] >> 8;
	u32 af_freq_khz;
	u32 tuned_freq_khz;
	struct kfifo *buff;
	struct af_list_ev ev;
	spinlock_t lock = radio->buf_lock[RTC6213N_FM_BUF_AF_LIST];

	rtc6213n_get_freq(radio, &tuned_freq_khz);

	for (; i < NO_OF_AF_IN_GRP; i++, af_data = radio->block[2] & 0xFF) {

		if (af_data >= MIN_AF_CNT_CODE && af_data <= MAX_AF_CNT_CODE) {

			pr_info("%s: resetting af info, freq %u, pi %u\n",
					__func__, tuned_freq_khz, radio->pi);
			radio->af_info2.inval_freq_cnt = 0;
			radio->af_info2.cnt = 0;
			radio->af_info2.orig_freq_khz = 0;

			/* AF count. */
			radio->af_info2.cnt = af_data - NO_AF_CNT_CODE;
			radio->af_info2.orig_freq_khz = tuned_freq_khz;
			radio->af_info2.pi = radio->pi;

			pr_info("%s: current freq is %u, AF cnt is %u\n",
					__func__, tuned_freq_khz, radio->af_info2.cnt);

		} else if (af_data >= MIN_AF_FREQ_CODE &&
				af_data <= MAX_AF_FREQ_CODE &&
				radio->af_info2.orig_freq_khz != 0 &&
				radio->af_info2.size < MAX_NO_OF_AF) {

			af_freq_khz = SCALE_AF_CODE_TO_FREQ_KHZ(af_data);
			retval = is_valid_freq(radio, af_freq_khz);
			if (retval == false) {
				pr_info("%s: Invalid AF\n", __func__);
				radio->af_info2.inval_freq_cnt++;
				continue;
			}

			retval = is_new_freq(radio, af_freq_khz);
			if (retval == false) {
				pr_info("%s: Duplicate AF\n", __func__);
				radio->af_info2.inval_freq_cnt++;
				continue;
			}

			/* update the AF list */
			radio->af_info2.af_list[radio->af_info2.size++] =
				af_freq_khz;
			pr_info("%s: AF is %u\n", __func__, af_freq_khz);
			if ((radio->af_info2.size +
						radio->af_info2.inval_freq_cnt ==
						radio->af_info2.cnt) &&
					is_different_af_list(radio)) {

				/* Copy the list to af_info1. */
				radio->af_info1.cnt = radio->af_info2.cnt;
				radio->af_info1.size = radio->af_info2.size;
				radio->af_info1.pi = radio->af_info2.pi;
				radio->af_info1.orig_freq_khz =
					radio->af_info2.orig_freq_khz;
				memset(radio->af_info1.af_list,
						0,
						sizeof(radio->af_info1.af_list));

				memcpy(radio->af_info1.af_list,
						radio->af_info2.af_list,
						sizeof(radio->af_info2.af_list));

				/* AF list changed, post it to user space */
				memset(&ev, 0, sizeof(struct af_list_ev));

				ev.tune_freq_khz =
					radio->af_info1.orig_freq_khz;
				ev.pi_code = radio->pi;
				ev.af_size = radio->af_info1.size;

				memcpy(&ev.af_list[0],
						radio->af_info1.af_list,
						GET_AF_LIST_LEN(ev.af_size));

				buff = &radio->data_buf[RTC6213N_FM_BUF_AF_LIST];
				kfifo_in_locked(buff,
						(u8 *)&ev,
						GET_AF_EVT_LEN(ev.af_size),
						&lock);

				pr_info("%s: posting AF list evt, curr freq %u\n",
						__func__, ev.tune_freq_khz);

				rtc6213n_q_event(radio,
						RTC6213N_EVT_NEW_AF_LIST);
			}
		}
	}
}

static void rtc6213n_update_ps(struct rtc6213n_device *radio, u8 addr, u8 ps)
{
	u8 i;
	bool ps_txt_chg = false;
	bool ps_cmplt = true;
	u8 *data;
	struct kfifo *data_b;

	pr_info("%s enter addr:%x ps:%x \n",__func__, addr, ps);

	if (radio->ps_tmp0[addr] == ps) {
		if (radio->ps_cnt[addr] < PS_VALIDATE_LIMIT) {
			radio->ps_cnt[addr]++;
		} else {
			radio->ps_cnt[addr] = PS_VALIDATE_LIMIT;
			radio->ps_tmp1[addr] = ps;
		}
	} else if (radio->ps_tmp1[addr] == ps) {
		if (radio->ps_cnt[addr] >= PS_VALIDATE_LIMIT) {
			ps_txt_chg = true;
			radio->ps_cnt[addr] = PS_VALIDATE_LIMIT + 1;
		} else {
			radio->ps_cnt[addr] = PS_VALIDATE_LIMIT;
		}
		radio->ps_tmp1[addr] = radio->ps_tmp0[addr];
		radio->ps_tmp0[addr] = ps;
	} else if (!radio->ps_cnt[addr]) {
		radio->ps_tmp0[addr] = ps;
		radio->ps_cnt[addr] = 1;
	} else {
		radio->ps_tmp1[addr] = ps;
	}

	if (ps_txt_chg) {
		for (i = 0; i < MAX_PS_LEN; i++) {
			if (radio->ps_cnt[i] > 1)
				radio->ps_cnt[i]--;
		}
	}

	for (i = 0; i < MAX_PS_LEN; i++) {
		if (radio->ps_cnt[i] < PS_VALIDATE_LIMIT) {
			pr_info("%s ps_cnt[%d] %d\n",__func__, i ,radio->ps_cnt[i]);
			ps_cmplt = false;
			return;
		}
	}

	if (ps_cmplt) {
		for (i = 0; (i < MAX_PS_LEN) &&
				(radio->ps_display[i] == radio->ps_tmp0[i]); i++)
			;
		if (i == MAX_PS_LEN) {
			pr_info("%s Same PS string repeated\n",__func__);
			return;
		}

		for (i = 0; i < MAX_PS_LEN; i++)
			radio->ps_display[i] = radio->ps_tmp0[i];

		data = kmalloc(PS_EVT_DATA_LEN, GFP_ATOMIC);
		if (data != NULL) {
			data[0] = NO_OF_PS;
			data[1] = radio->pty;
			data[2] = (radio->pi >> 8) & 0xFF;
			data[3] = (radio->pi & 0xFF);
			data[4] = 0;
			memcpy(data + OFFSET_OF_PS,
					radio->ps_tmp0, MAX_PS_LEN);
			data_b = &radio->data_buf[RTC6213N_FM_BUF_PS_RDS];
			kfifo_in_locked(data_b, data, PS_EVT_DATA_LEN,
					&radio->buf_lock[RTC6213N_FM_BUF_PS_RDS]);
			pr_info("%s Q the PS event\n", __func__);
			rtc6213n_q_event(radio, RTC6213N_EVT_NEW_PS_RDS);
			kfree(data);
		} else {
			pr_err("%s Memory allocation failed for PTY\n", __func__);
		}
	}
}

static void display_rt(struct rtc6213n_device *radio)
{
	u8 len = 0, i = 0;
	u8 *data;
	struct kfifo *data_b;
	bool rt_cmplt = true;

	pr_info("%s enter\n",__func__);

	for (i = 0; i < MAX_RT_LEN; i++) {
		if (radio->rt_cnt[i] < RT_VALIDATE_LIMIT) {
			pr_info("%s rt_cnt %d\n", __func__, radio->rt_cnt[i]);
			rt_cmplt = false;
			return;
		}
		if (radio->rt_tmp0[i] == END_OF_RT)
			break;
	}
	if (rt_cmplt) {
		while ((len < MAX_RT_LEN) && (radio->rt_tmp0[len] != END_OF_RT))
			len++;

		for (i = 0; (i < len) &&
				(radio->rt_display[i] == radio->rt_tmp0[i]); i++);
		if (i == len) {
			pr_info("%s Same RT string repeated\n",__func__);
			return;
		}
		for (i = 0; i < len; i++)
			radio->rt_display[i] = radio->rt_tmp0[i];
		data = kmalloc(len + OFFSET_OF_RT, GFP_ATOMIC);
		if (data != NULL) {
			data[0] = len; /* len of RT */
			data[1] = radio->pty;
			data[2] = (radio->pi >> 8) & 0xFF;
			data[3] = (radio->pi & 0xFF);
			data[4] = radio->rt_flag;
			memcpy(data + OFFSET_OF_RT, radio->rt_display, len);
			data_b = &radio->data_buf[RTC6213N_FM_BUF_RT_RDS];
			kfifo_in_locked(data_b, data, OFFSET_OF_RT + len,
					&radio->buf_lock[RTC6213N_FM_BUF_RT_RDS]);
			pr_info("%s Q the RT event\n", __func__);
			rtc6213n_q_event(radio, RTC6213N_EVT_NEW_RT_RDS);
			kfree(data);
		} else {
			pr_err("%s Memory allocation failed for PTY\n", __func__);
		}
	}
}

static void rt_handler(struct rtc6213n_device *radio, u8 ab_flg,
		u8 cnt, u8 addr, u8 *rt)
{
	u8 i, errcnt, blermax;
	bool rt_txt_chg = 0;

	pr_info("%s enter\n",__func__);

	if (ab_flg != radio->rt_flag && radio->valid_rt_flg) {
		for (i = 0; i < sizeof(radio->rt_cnt); i++) {
			if (!radio->rt_tmp0[i]) {
				radio->rt_tmp0[i] = ' ';
				radio->rt_cnt[i]++;
			}
		}
		memset(radio->rt_cnt, 0, sizeof(radio->rt_cnt));
		memset(radio->rt_tmp0, 0, sizeof(radio->rt_tmp0));
		memset(radio->rt_tmp1, 0, sizeof(radio->rt_tmp1));
	}

	radio->rt_flag = ab_flg;
	radio->valid_rt_flg = true;

	for (i = 0; i < cnt; i++) {
		if((i < 2) && (cnt > 2)){
			errcnt = radio->bler[2];
			blermax = CORRECTED_THREE_TO_FIVE;
		} else {
			errcnt = radio->bler[3];
			blermax = CORRECTED_THREE_TO_FIVE;
		}
		if(errcnt <= blermax) {
			if(!rt[i])
				rt[i] = ' ';
			if (radio->rt_tmp0[addr+i] == rt[i]) {
				if (radio->rt_cnt[addr+i] < RT_VALIDATE_LIMIT) {
					radio->rt_cnt[addr+i]++;
				} else {
					radio->rt_cnt[addr+i] = RT_VALIDATE_LIMIT;
					radio->rt_tmp1[addr+i] = rt[i];
				}
			} else if (radio->rt_tmp1[addr+i] == rt[i]) {
				if (radio->rt_cnt[addr+i] >= RT_VALIDATE_LIMIT) {
					rt_txt_chg = true;
					radio->rt_cnt[addr+i] = RT_VALIDATE_LIMIT + 1;
				} else {
					radio->rt_cnt[addr+i] = RT_VALIDATE_LIMIT;
				}
				radio->rt_tmp1[addr+i] = radio->rt_tmp0[addr+i];
				radio->rt_tmp0[addr+i] = rt[i];
			} else if (!radio->rt_cnt[addr+i]) {
				radio->rt_tmp0[addr+i] = rt[i];
				radio->rt_cnt[addr+i] = 1;
			} else {
				radio->rt_tmp1[addr+i] = rt[i];
			}
		}
	}

	if (rt_txt_chg) {
		for (i = 0; i < MAX_RT_LEN; i++) {
			if (radio->rt_cnt[i] > 1)
				radio->rt_cnt[i]--;
		}
	}
	display_rt(radio);
}

static void rtc6213n_raw_rds(struct rtc6213n_device *radio)
{
	u16 aid, app_grp_typ;

	aid = radio->block[3];
	app_grp_typ = radio->block[1] & APP_GRP_typ_MASK;
	pr_info("%s app_grp_typ = %x\n", __func__, app_grp_typ);
	pr_info("%s AID = %x", __func__, aid);

	switch (aid) {
		case ERT_AID:
			radio->utf_8_flag = (radio->block[2] & 1);
			radio->formatting_dir = EXTRACT_BIT(radio->block[2],
					ERT_FORMAT_DIR_BIT);
			if (radio->ert_carrier != app_grp_typ) {
				rtc6213n_q_event(radio, RTC6213N_EVT_NEW_ODA);
				radio->ert_carrier = app_grp_typ;
			}
			break;
		case RT_PLUS_AID:
			/*Extract 5th bit of MSB (b7b6b5b4b3b2b1b0)*/
			radio->rt_ert_flag = EXTRACT_BIT(radio->block[2],
					RT_ERT_FLAG_BIT);
			if (radio->rt_plus_carrier != app_grp_typ) {
				rtc6213n_q_event(radio, RTC6213N_EVT_NEW_ODA);
				radio->rt_plus_carrier = app_grp_typ;
			}
			break;
		default:
			pr_info("%s Not handling the AID of %x\n", __func__, aid);
			break;
	}
}

static void rtc6213n_ev_ert(struct rtc6213n_device *radio)
{
	u8 *data = NULL;
	struct kfifo *data_b;

	if (radio->ert_len <= 0)
		return;

	pr_info("%s enter\n", __func__);
	data = kmalloc((radio->ert_len + ERT_OFFSET), GFP_ATOMIC);
	if (data != NULL) {
		data[0] = radio->ert_len;
		data[1] = radio->utf_8_flag;
		data[2] = radio->formatting_dir;
		memcpy((data + ERT_OFFSET), radio->ert_buf, radio->ert_len);
		data_b = &radio->data_buf[RTC6213N_FM_BUF_ERT];
		kfifo_in_locked(data_b, data, (radio->ert_len + ERT_OFFSET),
				&radio->buf_lock[RTC6213N_FM_BUF_ERT]);
		rtc6213n_q_event(radio, RTC6213N_EVT_NEW_ERT);
		kfree(data);
	}
}

static void rtc6213n_buff_ert(struct rtc6213n_device *radio)
{
	int i;
	u16 info_byte = 0;
	u8 byte_pair_index;

	byte_pair_index = radio->block[1] & APP_GRP_typ_MASK;
	if (byte_pair_index == 0) {
		radio->c_byt_pair_index = 0;
		radio->ert_len = 0;
	}
	if (radio->c_byt_pair_index == byte_pair_index) {
		for (i = 2; i <= 3; i++) {
			info_byte = radio->block[i];
			pr_info("%s info_byte = %x\n", __func__, info_byte);
			pr_info("%s ert_len = %x\n", __func__, radio->ert_len);
			if (radio->ert_len > (MAX_ERT_LEN - 2))
				return;
			radio->ert_buf[radio->ert_len] = radio->block[i] >> 8;
			radio->ert_buf[radio->ert_len + 1] =
				radio->block[i] & 0xFF;
			radio->ert_len += ERT_CNT_PER_BLK;
			pr_info("%s utf_8_flag = %d\n", __func__, radio->utf_8_flag);
			if ((radio->utf_8_flag == 0) &&
					(info_byte == END_OF_RT)) {
				radio->ert_len -= ERT_CNT_PER_BLK;
				break;
			} else if ((radio->utf_8_flag == 1) &&
					(radio->block[i] >> 8 == END_OF_RT)) {
				info_byte = END_OF_RT;
				radio->ert_len -= ERT_CNT_PER_BLK;
				break;
			} else if ((radio->utf_8_flag == 1) &&
					((radio->block[i] & 0xFF)
					 == END_OF_RT)) {
				info_byte = END_OF_RT;
				radio->ert_len--;
				break;
			}
		}
		if ((byte_pair_index == MAX_ERT_SEGMENT) ||
				(info_byte == END_OF_RT)) {
			rtc6213n_ev_ert(radio);
			radio->c_byt_pair_index = 0;
			radio->ert_len = 0;
		}
		radio->c_byt_pair_index++;
	} else {
		radio->ert_len = 0;
		radio->c_byt_pair_index = 0;
	}
}

static void rtc6213n_rt_plus(struct rtc6213n_device *radio)
{
	u8 tag_type1, tag_type2;
	u8 *data = NULL;
	int len = 0;
	u16 grp_typ;
	struct kfifo *data_b;

	grp_typ = radio->block[1] & APP_GRP_typ_MASK;
	/*
	 *right most 3 bits of Lsb of block 2
	 * and left most 3 bits of Msb of block 3
	 */
	tag_type1 = (((grp_typ & TAG1_MSB_MASK) << TAG1_MSB_OFFSET) |
			(radio->block[2] >> TAG1_LSB_OFFSET));
	/*
	 *right most 1 bit of lsb of 3rd block
	 * and left most 5 bits of Msb of 4th block
	 */
	tag_type2 = (((radio->block[2] & TAG2_MSB_MASK)
				<< TAG2_MSB_OFFSET) |
			(radio->block[2] >> TAG2_LSB_OFFSET));

	if (tag_type1 != DUMMY_CLASS)
		len += RT_PLUS_LEN_1_TAG;
	if (tag_type2 != DUMMY_CLASS)
		len += RT_PLUS_LEN_1_TAG;

	if (len != 0) {
		len += RT_PLUS_OFFSET;
		data = kmalloc(len, GFP_ATOMIC);
	} else {
		pr_err("%s:Len is zero\n", __func__);
		return;
	}
	if (data != NULL) {
		data[0] = len;
		len = RT_ERT_FLAG_OFFSET;
		data[len++] = radio->rt_ert_flag;
		if (tag_type1 != DUMMY_CLASS) {
			data[len++] = tag_type1;
			/*
			 *start position of tag1
			 *right most 5 bits of msb of 3rd block
			 *and left most bit of lsb of 3rd block
			 */
			data[len++] = (radio->block[2] >> TAG1_POS_LSB_OFFSET)
				& TAG1_POS_MSB_MASK;
			/*
			 *length of tag1
			 *left most 6 bits of lsb of 3rd block
			 */
			data[len++] = (radio->block[2] >> TAG1_LEN_OFFSET) &
				TAG1_LEN_MASK;
		}
		if (tag_type2 != DUMMY_CLASS) {
			data[len++] = tag_type2;
			/*
			 *start position of tag2
			 *right most 3 bit of msb of 4th block
			 *and left most 3 bits of lsb of 4th block
			 */
			data[len++] = (radio->block[3] >> TAG2_POS_LSB_OFFSET) &
				TAG2_POS_MSB_MASK;
			/*
			 *length of tag2
			 *right most 5 bits of lsb of 4th block
			 */
			data[len++] = radio->block[3] & TAG2_LEN_MASK;
		}
		data_b = &radio->data_buf[RTC6213N_FM_BUF_RT_PLUS];
		kfifo_in_locked(data_b, data, len,
				&radio->buf_lock[RTC6213N_FM_BUF_RT_PLUS]);
		rtc6213n_q_event(radio, RTC6213N_EVT_NEW_RT_PLUS);
		kfree(data);
	} else {
		pr_err("%s:memory allocation failed\n", __func__);
	}
}

void rtc6213n_rds_handler(struct work_struct *worker)
{
	struct rtc6213n_device *radio;
	u8 rt_blks[NO_OF_RDS_BLKS];
	u8 grp_type, addr, ab_flg;

	radio = container_of(worker, struct rtc6213n_device, rds_worker);

	if (!radio) {
		pr_err("%s:radio is null\n", __func__);
		return;
	}

	pr_info("%s enter\n", __func__);

	rtc6213n_get_rds(radio);

	if(radio->bler[0] < CORRECTED_THREE_TO_FIVE)
		rtc6213n_pi_check(radio, radio->block[0]);

	if(radio->bler[1] < CORRECTED_ONE_TO_TWO){
		grp_type = radio->block[1] >> OFFSET_OF_GRP_TYP;
		pr_info("%s grp_type = %d\n", __func__, grp_type);
	} else {
		pr_err("%s invalid data\n",__func__);
		return;
	}
	if (grp_type & 0x01)
		rtc6213n_pi_check(radio, radio->block[2]);

	rtc6213n_pty_check(radio, (radio->block[1] >> OFFSET_OF_PTY) & PTY_MASK);

	switch (grp_type) {
		case RDS_TYPE_0A:
			if(radio->bler[2] <= CORRECTED_THREE_TO_FIVE)
				rtc6213n_update_af_list(radio);
			/*  fall through */
		case RDS_TYPE_0B:
			addr = (radio->block[1] & PS_MASK) * NO_OF_CHARS_IN_EACH_ADD;
			pr_info("%s RDS is PS\n", __func__);
			if(radio->bler[3] <= CORRECTED_THREE_TO_FIVE){
				rtc6213n_update_ps(radio, addr+0, radio->block[3] >> 8);
				rtc6213n_update_ps(radio, addr+1, radio->block[3] & 0xff);
			}
			break;
		case RDS_TYPE_2A:
			pr_info("%s RDS is RT 2A group\n", __func__);
			rt_blks[0] = (u8)(radio->block[2] >> 8);
			rt_blks[1] = (u8)(radio->block[2] & 0xFF);
			rt_blks[2] = (u8)(radio->block[3] >> 8);
			rt_blks[3] = (u8)(radio->block[3] & 0xFF);
			addr = (radio->block[1] & 0xf) * 4;
			ab_flg = (radio->block[1] & 0x0010) >> 4;
			rt_handler(radio, ab_flg, CNT_FOR_2A_GRP_RT, addr, rt_blks);
			break;
		case RDS_TYPE_2B:
			pr_info("%s RDS is RT 2B group\n",__func__);
			rt_blks[0] = (u8)(radio->block[3] >> 8);
			rt_blks[1] = (u8)(radio->block[3] & 0xFF);
			rt_blks[2] = 0;
			rt_blks[3] = 0;
			addr = (radio->block[1] & 0xf) * 2;
			ab_flg = (radio->block[1] & 0x0010) >> 4;
			radio->rt_tmp0[MAX_LEN_2B_GRP_RT] = END_OF_RT;
			radio->rt_tmp1[MAX_LEN_2B_GRP_RT] = END_OF_RT;
			radio->rt_cnt[MAX_LEN_2B_GRP_RT] = RT_VALIDATE_LIMIT;
			rt_handler(radio, ab_flg, CNT_FOR_2B_GRP_RT, addr, rt_blks);
			break;
		case RDS_TYPE_3A:
			pr_info("%s RDS is 3A group\n",__func__);
			rtc6213n_raw_rds(radio);
			break;
		default:
			pr_err("%s Not handling the group type %d\n", __func__, grp_type);
			break;
	}
	pr_info("%s rt_plus_carrier = %x\n", __func__, radio->rt_plus_carrier);
	pr_info("%s ert_carrier = %x\n", __func__, radio->ert_carrier);
	if (radio->rt_plus_carrier && (grp_type == radio->rt_plus_carrier))
		rtc6213n_rt_plus(radio);
	else if (radio->ert_carrier && (grp_type == radio->ert_carrier))
		rtc6213n_buff_ert(radio);
	return;
}

/*
 * rtc6213n_rds_on - switch on rds reception
 */
static int rtc6213n_rds_on(struct rtc6213n_device *radio)
{
	int retval;

	pr_info("%s enter\n", __func__);
	/* sysconfig */
	radio->registers[SYSCFG] |= SYSCFG_CSR0_RDS_EN;
	retval = rtc6213n_set_register(radio, SYSCFG);

	if (retval < 0)
		radio->registers[SYSCFG] &= ~SYSCFG_CSR0_RDS_EN;

	return retval;
}

int rtc6213n_reset_rds_data(struct rtc6213n_device *radio)
{
	int retval = 0;

	mutex_lock(&radio->lock);

	radio->wr_index = 0;
	radio->rd_index = 0;
	memset(radio->buffer, 0, radio->buf_size);
	mutex_unlock(&radio->lock);

	return retval;
}

int rtc6213n_power_down(struct rtc6213n_device *radio)
{
	int retval = 0;

	pr_info("%s enter\n",__func__);

	mutex_lock(&radio->lock);
		/* stop radio */
	retval = rtc6213n_stop(radio);

	rtc6213n_disable_irq(radio);
	mutex_unlock(&radio->lock);
	pr_info("%s exit %d\n", __func__, retval);

	return retval;
}

int rtc6213n_power_up(struct rtc6213n_device *radio)
{
	int retval = 0;

	mutex_lock(&radio->lock);

	pr_info("%s enter\n", __func__);

	/* start radio */
	retval = rtc6213n_start(radio);
	if (retval < 0)
		goto done;
	pr_info("%s after initialization\n", __func__);

	/* Enable FM */
	radio->registers[POWERCFG] = POWERCFG_CSR0_ENABLE;
	radio->registers[POWERCFG] &= ~POWERCFG_CSR0_DISABLE;
	retval = rtc6213n_set_register(radio, POWERCFG);
	if (retval < 0)
		goto done;

	msleep(110);

	/* mpxconfig */
	/* Disable Softmute / Disable Mute / De-emphasis / Volume 8 */
	radio->registers[MPXCFG] = 0xf |
		MPXCFG_CSR0_DIS_SMUTE | MPXCFG_CSR0_DIS_MUTE |
		((de << 12) & MPXCFG_CSR0_DEEM);
	retval = rtc6213n_set_register(radio, MPXCFG);
	if (retval < 0)
		goto done;

	/* channel */
	/* Band / Space / Default channel 90.1Mhz */
	radio->registers[CHANNEL] =
		((band  << 12) & CHANNEL_CSR0_BAND)  |
		((space << 10) & CHANNEL_CSR0_CHSPACE) | 0x1a;
	retval = rtc6213n_set_register(radio, CHANNEL);
	if (retval < 0)
		goto done;

	/* seekconfig2 */
	/* Seeking TH */
	radio->registers[SEEKCFG2] = 0x4050;
	retval = rtc6213n_set_register(radio, SEEKCFG2);
	if (retval < 0)
		goto done;

	/* enable RDS / STC interrupt */
	radio->registers[SYSCFG] |= SYSCFG_CSR0_RDSIRQEN;
	radio->registers[SYSCFG] |= SYSCFG_CSR0_STDIRQEN;
	/*radio->registers[SYSCFG] |= SYSCFG_CSR0_RDS_EN;*/
	retval = rtc6213n_set_register(radio, SYSCFG);
	if (retval < 0)
		goto done;

	radio->registers[PADCFG] &= ~PADCFG_CSR0_GPIO;
	radio->registers[PADCFG] |= 0x1 << 2;
	retval = rtc6213n_set_register(radio, PADCFG);
	if (retval < 0)
		goto done;

done:
	pr_info("%s exit %d\n", __func__, retval);
	mutex_unlock(&radio->lock);
	return retval;
}

/**************************************************************************
 * File Operations Interface
 **************************************************************************/

/*
 * rtc6213n_fops_read - read RDS data
 */
static ssize_t rtc6213n_fops_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;
	unsigned int block_count = 0;

	/* switch on rds reception */
	mutex_lock(&radio->lock);
	/* if RDS is not on, then turn on RDS */
	if ((radio->registers[SYSCFG] & SYSCFG_CSR0_RDS_EN) == 0)
		rtc6213n_rds_on(radio);

	/* block if no new data available */
	while (radio->wr_index == radio->rd_index) {
		if (file->f_flags & O_NONBLOCK) {
			retval = -EWOULDBLOCK;
			goto done;
		}
		if (wait_event_interruptible(radio->read_queue,
				radio->wr_index != radio->rd_index) < 0) {
			retval = -EINTR;
			goto done;
		}
	}

	/* calculate block count from byte count */
	count /= 3;
	#ifdef _RDSDEBUG
	dev_info(&radio->videodev.dev, "rtc6213n_fops_read : count = %zu\n",
		count);
	#endif

	/* copy RDS block out of internal buffer and to user buffer */
	while (block_count < count) {
		if (radio->rd_index == radio->wr_index)
			break;
		/* always transfer rds complete blocks */
		if (copy_to_user(buf, &radio->buffer[radio->rd_index], 3))
			/* retval = -EFAULT; */
			break;
		/* increment and wrap read pointer */
		radio->rd_index += 3;
		if (radio->rd_index >= radio->buf_size)
			radio->rd_index = 0;
		/* increment counters */
		block_count++;
		buf += 3;
		retval += 3;
		#ifdef _RDSDEBUG
		dev_info(&radio->videodev.dev, "rtc6213n_fops_read : block_count = %d, count = %zu\n",
			block_count, count);
		#endif
	}

done:
	mutex_unlock(&radio->lock);
	return retval;
}

/*
 * rtc6213n_fops_poll - poll RDS data
 */
static unsigned int rtc6213n_fops_poll(struct file *file,
		struct poll_table_struct *pts)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	/* switch on rds reception */
	mutex_lock(&radio->lock);
	if ((radio->registers[SYSCFG] & SYSCFG_CSR0_RDS_EN) == 0)
		rtc6213n_rds_on(radio);
	mutex_unlock(&radio->lock);

	poll_wait(file, &radio->read_queue, pts);

	if (radio->rd_index != radio->wr_index)
		retval = POLLIN | POLLRDNORM;

	return retval;
}

/* static */
int rtc6213n_vidioc_g_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	pr_info("%s enter, ctrl->id: %x, value:%d \n", __func__, ctrl->id, ctrl->value);

	mutex_lock(&radio->lock);

	switch (ctrl->id) {
	case V4L2_CID_PRIVATE_RTC6213N_STATE:
		break;
	case V4L2_CID_PRIVATE_RTC6213N_RDSGROUP_PROC:
		break;
	case V4L2_CID_PRIVATE_CSR0_ENABLE:
		dev_info(&radio->videodev.dev,
			"V4L2_CID_PRIVATE_CSR0_ENABLE val=%d\n", ctrl->value);
		break;
	case V4L2_CID_PRIVATE_CSR0_DISABLE:
		dev_info(&radio->videodev.dev,
			"V4L2_CID_PRIVATE_CSR0_DISABLE val=%d\n", ctrl->value);
		break;
	case V4L2_CID_PRIVATE_CSR0_VOLUME:
	case V4L2_CID_AUDIO_VOLUME:
	#ifdef New_VolumeControl
		ctrl->value = global_volume;
	#else
		ctrl->value = radio->registers[MPXCFG] & MPXCFG_CSR0_VOLUME;
	#endif
		break;
	case V4L2_CID_PRIVATE_CSR0_DIS_MUTE:
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = ((radio->registers[MPXCFG] &
			MPXCFG_CSR0_DIS_MUTE) == 0) ? 1 : 0;
		break;
	case V4L2_CID_PRIVATE_CSR0_DIS_SMUTE:
		ctrl->value = ((radio->registers[MPXCFG] &
			MPXCFG_CSR0_DIS_SMUTE) == 0) ? 1 : 0;
		break;
	case V4L2_CID_PRIVATE_CSR0_BAND:
		ctrl->value = ((radio->registers[CHANNEL] &
			CHANNEL_CSR0_BAND) >> 12);
		break;
	case V4L2_CID_PRIVATE_CSR0_SEEKRSSITH:
		ctrl->value = radio->registers[SEEKCFG1] &
			SEEKCFG1_CSR0_SEEKRSSITH;
		break;
	/* VIDIO_G_CTRL described end */
	case V4L2_CID_PRIVATE_RSSI:
		rtc6213n_get_all_registers(radio);
		ctrl->value = radio->registers[RSSI] & RSSI_RSSI;
		dev_info(&radio->videodev.dev, "Get V4L2_CONTROL V4L2_CID_PRIVATE_RSSI: STATUS=0x%4.4hx RSSI = %d\n",
			radio->registers[STATUS],
			radio->registers[RSSI] & RSSI_RSSI);
		dev_info(&radio->videodev.dev, "Get V4L2_CONTROL V4L2_CID_PRIVATE_RSSI: regC=0x%4.4hx RegD=0x%4.4hx\n",
			radio->registers[BA_DATA], radio->registers[BB_DATA]);
		dev_info(&radio->videodev.dev, "Get V4L2_CONTROL V4L2_CID_PRIVATE_RSSI: regE=0x%4.4hx RegF=0x%4.4hx\n",
			radio->registers[BC_DATA], radio->registers[BD_DATA]);
		break;
	case V4L2_CID_PRIVATE_DEVICEID:
		ctrl->value = radio->registers[DEVICEID] & DEVICE_ID;
		dev_info(&radio->videodev.dev, "Get V4L2_CONTROL V4L2_CID_PRIVATE_DEVICEID: DEVICEID=0x%4.4hx\n",
			radio->registers[DEVICEID]);
		break;
	case V4L2_CID_PRIVATE_CSR0_OFSTH:
		rtc6213n_get_all_registers(radio);
		ctrl->value = ((radio->registers[SEEKCFG2] &
			SEEKCFG2_CSR0_OFSTH) >> 8);
		dev_info(&radio->videodev.dev,
			"Get V4L2_CONTROL V4L2_CID_PRIVATE_CSR0_OFSTH :	SEEKCFG2=0x%4.4hx\n",
			radio->registers[SEEKCFG2]);
		break;
	case V4L2_CID_PRIVATE_CSR0_QLTTH:
		rtc6213n_get_all_registers(radio);
		ctrl->value = radio->registers[SEEKCFG2] & SEEKCFG2_CSR0_QLTTH;
		dev_info(&radio->videodev.dev,
		"V4L2_CID_PRIVATE_CSR0_QLTTH : SEEKCFG2=0x%4.4hx\n",
			radio->registers[SEEKCFG2]);
		break;
	default:
		pr_info("%s in default id:%d\n", __func__, ctrl->id);
		retval = -EINVAL;
	}

	mutex_unlock(&radio->lock);
	return retval;
}

static int rtc6213n_vidioc_dqbuf(struct file *file, void *priv,
		struct v4l2_buffer *buffer)
{

	struct rtc6213n_device *radio = video_get_drvdata(video_devdata(file));
	enum rtc6213n_buf_t buf_type = -1;
	u8 buf_fifo[STD_BUF_SIZE] = {0};
	struct kfifo *data_fifo = NULL;
	u8 *buf = NULL;
	int len = 0, retval = -1;

	if ((radio == NULL) || (buffer == NULL)) {
		pr_err("%s radio/buffer is NULL\n",__func__);
		return -ENXIO;
	}
	buf_type = buffer->index;
	buf = (u8 *)buffer->m.userptr;
	len = buffer->length;
	pr_info("%s: requesting buffer %d\n", __func__, buf_type);

	if ((buf_type < RTC6213N_FM_BUF_MAX) && (buf_type >= 0)) {
		data_fifo = &radio->data_buf[buf_type];
		if (buf_type == RTC6213N_FM_BUF_EVENTS) {
			if (wait_event_interruptible(radio->event_queue,
						kfifo_len(data_fifo)) < 0) {
				return -EINTR;
			}
		}
	} else {
		pr_err("%s invalid buffer type\n",__func__);
		return -EINVAL;
	}
	if (len <= STD_BUF_SIZE) {
		buffer->bytesused = kfifo_out_locked(data_fifo, &buf_fifo[0],
				len, &radio->buf_lock[buf_type]);
	} else {
		pr_err("%s kfifo_out_locked can not use len more than 128\n",__func__);
		return -EINVAL;
	}
	retval = copy_to_user(buf, &buf_fifo[0], buffer->bytesused);
	if (retval > 0) {
		pr_err("%s Failed to copy %d bytes of data\n", __func__, retval);
		return -EAGAIN;
	}

	return retval;
}

static bool check_mode(struct rtc6213n_device *radio)
{
	bool retval = true;

	if (radio->mode == FM_OFF || radio->mode == FM_RECV)
		retval = false;

	return retval;
}



static int rtc6213n_disable(struct rtc6213n_device *radio)
{
	int retval = 0;

	/* disable RDS/STC interrupt */
	radio->registers[SYSCFG] |= SYSCFG_CSR0_RDSIRQEN;
	radio->registers[SYSCFG] |= SYSCFG_CSR0_STDIRQEN;
	retval = rtc6213n_set_register(radio, SYSCFG);
	if(retval < 0){
		pr_err("%s fail to disable RDS/SCT interrupt\n",__func__);
		goto done;
	}
	retval = rtc6213n_power_down(radio);
	if(retval < 0){
		pr_err("%s fail to turn off fmradio\n",__func__);
		goto done;
	}

	if (radio->mode == FM_TURNING_OFF || radio->mode == FM_RECV) {
		pr_info("%s: posting RTC6213N_EVT_RADIO_DISABLED event\n",
				__func__);
		rtc6213n_q_event(radio, RTC6213N_EVT_RADIO_DISABLED);
		radio->mode = FM_OFF;
	}
//	flush_workqueue(radio->wqueue);

done:
	return retval;
}

static int rtc6213n_enable(struct rtc6213n_device *radio)
{
	int retval = 0;

	retval = rtc6213n_get_register(radio, POWERCFG);
	if((radio->registers[POWERCFG] & POWERCFG_CSR0_ENABLE)== 0){
		rtc6213n_power_up(radio);
	} else {
		pr_info("%s already turn on\n",__func__);
	}

	if((radio->registers[SYSCFG] &  SYSCFG_CSR0_STDIRQEN ) == 0){
		radio->registers[SYSCFG] |= SYSCFG_CSR0_RDSIRQEN;
		radio->registers[SYSCFG] |= SYSCFG_CSR0_STDIRQEN;
		retval = rtc6213n_set_register(radio, SYSCFG);
		if (retval < 0) {
			pr_err("%s set register fail\n",__func__);
			goto done;
		} else{
			rtc6213n_q_event(radio, RTC6213N_EVT_RADIO_READY);
			radio->mode = FM_RECV;
		}
	} else {
		rtc6213n_q_event(radio, RTC6213N_EVT_RADIO_READY);
		radio->mode = FM_RECV;
	}
done:
	return retval;

}

bool rtc6213n_is_valid_srch_mode(int srch_mode)
{
	if ((srch_mode >= RTC6213N_MIN_SRCH_MODE) &&
			(srch_mode <= RTC6213N_MAX_SRCH_MODE))
		return 1;
	else
		return 0;
}

/*
 * rtc6213n_vidioc_s_ctrl - set the value of a control
 */
int rtc6213n_vidioc_s_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;
	int space_s = 0;
	pr_info("%s enter, ctrl->id: %x, value:%d \n", __func__, ctrl->id, ctrl->value);

	switch (ctrl->id) {
		case V4L2_CID_PRIVATE_RTC6213N_STATE:
			if (ctrl->value == FM_RECV) {
				if (check_mode(radio) != 0) {
					pr_err("%s: fm is not in proper state\n",
							__func__);
					retval = -EINVAL;
					goto end;
				}
				radio->mode = FM_RECV_TURNING_ON;

				retval = rtc6213n_enable(radio);
				if (retval < 0) {
					pr_err("%s Error while enabling RECV FM %d\n",
							__func__, retval);
					radio->mode = FM_OFF;
					goto end;
				}
			} else if (ctrl->value == FM_OFF) {
				radio->mode = FM_TURNING_OFF;
				retval = rtc6213n_disable(radio);
				if (retval < 0) {
					pr_err("Err on disable recv FM %d\n", retval);
					radio->mode = FM_RECV;
					goto end;
				}
			}
			break;
		case V4L2_CID_PRIVATE_RTC6213N_SET_AUDIO_PATH:
		case V4L2_CID_PRIVATE_RTC6213N_SRCH_ALGORITHM:
		case V4L2_CID_PRIVATE_RTC6213N_REGION:
			retval = 0;
			break;
		case V4L2_CID_PRIVATE_RTC6213N_EMPHASIS:
			if (ctrl->value == 1)
				radio->registers[MPXCFG] |= MPXCFG_CSR0_DEEM;
			else
				radio->registers[MPXCFG] &= ~MPXCFG_CSR0_DEEM;
			retval = rtc6213n_set_register(radio, MPXCFG);
			break;
		case V4L2_CID_PRIVATE_RTC6213N_RDS_STD:
			/* enable RDS / STC interrupt */
			radio->registers[SYSCFG] |= SYSCFG_CSR0_RDSIRQEN;
			radio->registers[SYSCFG] |= SYSCFG_CSR0_STDIRQEN;
			/*radio->registers[SYSCFG] |= SYSCFG_CSR0_RDS_EN;*/
			retval = rtc6213n_set_register(radio, SYSCFG);
			break;
		case V4L2_CID_PRIVATE_RTC6213N_SPACING:
			space_s = ctrl->value;
			radio->space = ctrl->value;
			radio->registers[CHANNEL] &= ~CHANNEL_CSR0_CHSPACE;
			radio->registers[CHANNEL] |= (space_s << 10);
			retval = rtc6213n_set_register(radio, CHANNEL);
			break;
		case V4L2_CID_PRIVATE_RTC6213N_LP_MODE:
		case V4L2_CID_PRIVATE_RTC6213N_ANTENNA:
		case V4L2_CID_PRIVATE_RTC6213N_RDSON:
		case V4L2_CID_PRIVATE_RTC6213N_AF_JUMP:
		case V4L2_CID_PRIVATE_RTC6213N_SRCH_CNT:
		case V4L2_CID_PRIVATE_RTC6213N_RXREPEATCOUNT:
			retval = 0;
			break;
		case V4L2_CID_PRIVATE_RTC6213N_SINR_THRESHOLD:
			radio->registers[SEEKCFG1] &= ~SEEKCFG1_CSR0_SEEKRSSITH;
			radio->registers[SEEKCFG1] |= 0x0;
			retval = rtc6213n_set_register(radio, SEEKCFG1);
			break;
		case V4L2_CID_PRIVATE_RTC6213N_RDSGROUP_MASK:
		case V4L2_CID_PRIVATE_RTC6213N_RDSGROUP_PROC:
			if((radio->registers[SYSCFG] & SYSCFG_CSR0_RDS_EN) == 0)
			rtc6213n_rds_on(radio);
			retval = 0;
			break;
		case V4L2_CID_PRIVATE_RTC6213N_SRCHMODE:
			if (rtc6213n_is_valid_srch_mode(ctrl->value)) {
				radio->g_search_mode = ctrl->value;
			} else {
				pr_err("%s: srch mode is not valid\n", __func__);
				retval = -EINVAL;
				goto end;
			}
			break;
		case V4L2_CID_PRIVATE_RTC6213N_PSALL:
			break;
		case V4L2_CID_PRIVATE_RTC6213N_SCANDWELL:
			if ((ctrl->value >= MIN_DWELL_TIME) &&
					(ctrl->value <= MAX_DWELL_TIME)) {
				radio->dwell_time_sec = ctrl->value;
			} else {
				pr_err("%s: scandwell period is not valid\n", __func__);
				retval = -EINVAL;
			}
			break;
		case V4L2_CID_PRIVATE_CSR0_ENABLE:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_ENABLE val=%d\n",
					ctrl->value);
			retval = rtc6213n_power_up(radio);
			/* must keep below line */
			ctrl->value = 0;
			break;
		case V4L2_CID_PRIVATE_CSR0_DISABLE:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_DISABLE val=%d\n",
					ctrl->value);
			retval = rtc6213n_power_down(radio);
			/* must keep below line */
			ctrl->value = 0;
			break;
		case V4L2_CID_PRIVATE_DEVICEID:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_DEVICEID val=%d\n",
					ctrl->value);
			break;
		case V4L2_CID_PRIVATE_CSR0_VOLUME:
		case V4L2_CID_AUDIO_VOLUME:
			dev_info(&radio->videodev.dev,
					"V4L2_CID_AUDIO_VOLUME : MPXCFG=0x%4.4hx POWERCFG=0x%4.4hx\n",
					radio->registers[MPXCFG], radio->registers[POWERCFG]);
#ifdef New_VolumeControl
			global_volume = ctrl->value;

			radio->registers[POWERCFG] = (ctrl->value < 9) ?
				(radio->registers[POWERCFG] | 0x0008) :
				(radio->registers[POWERCFG] & 0xFFF7);
			if (ctrl->value == 8)
				retval = rtc6213n_set_register(radio, POWERCFG);
			radio->registers[MPXCFG] &= ~MPXCFG_CSR0_VOLUME;
			radio->registers[MPXCFG] |=
				(ctrl->value < 9) ? ((ctrl->value == 0) ? 0 :
						(2*ctrl->value - 1)) : ctrl->value;
			dev_info(&radio->videodev.dev, "V4L2_CID_AUDIO_VOLUME : MPXCFG=0x%4.4hx POWERCFG=0x%4.4hx\n",
					radio->registers[MPXCFG], radio->registers[POWERCFG]);
			retval = rtc6213n_set_register(radio, POWERCFG);
#else
			radio->registers[MPXCFG] &= ~MPXCFG_CSR0_VOLUME;
			radio->registers[MPXCFG] |=
				(ctrl->value > 15) ? 8 : ctrl->value;
			dev_info(&radio->videodev.dev, "V4L2_CID_AUDIO_VOLUME : MPXCFG=0x%4.4hx POWERCFG=0x%4.4hx\n",
					radio->registers[MPXCFG], radio->registers[POWERCFG]);
			retval = rtc6213n_set_register(radio, MPXCFG);
#endif
			break;
		case V4L2_CID_PRIVATE_CSR0_DIS_MUTE:
		case V4L2_CID_AUDIO_MUTE:
			if (ctrl->value == 1)
				radio->registers[MPXCFG] &= ~MPXCFG_CSR0_DIS_MUTE;
			else
				radio->registers[MPXCFG] |= MPXCFG_CSR0_DIS_MUTE;
			retval = rtc6213n_set_register(radio, MPXCFG);
			break;
		case V4L2_CID_PRIVATE_CSR0_DIS_SMUTE:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_DIS_SMUTE : before V4L2_CID_PRIVATE_CSR0_DIS_SMUTE\n");
			if (ctrl->value == 1)
				radio->registers[MPXCFG] &= ~MPXCFG_CSR0_DIS_SMUTE;
			else
				radio->registers[MPXCFG] |= MPXCFG_CSR0_DIS_SMUTE;
			retval = rtc6213n_set_register(radio, MPXCFG);
			break;
		case V4L2_CID_PRIVATE_CSR0_DEEM:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_DEEM : before V4L2_CID_PRIVATE_CSR0_DEEM\n");
			if (ctrl->value == 1)
				radio->registers[MPXCFG] |= MPXCFG_CSR0_DEEM;
			else
				radio->registers[MPXCFG] &= ~MPXCFG_CSR0_DEEM;
			retval = rtc6213n_set_register(radio, MPXCFG);
			break;
		case V4L2_CID_PRIVATE_CSR0_BLNDADJUST:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_BLNDADJUST val=%d\n",
					ctrl->value);
			break;
		case V4L2_CID_PRIVATE_CSR0_BAND:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_BAND : CHANNEL=0x%4.4hx SEEKCFG1=0x%4.4hx\n",
					radio->registers[CHANNEL], radio->registers[SEEKCFG1]);
			radio->registers[CHANNEL] &= ~CHANNEL_CSR0_BAND;
			radio->registers[CHANNEL] |= (ctrl->value << 12);
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_BAND : CHANNEL=0x%4.4hx SEEKCFG1=0x%4.4hx\n",
					radio->registers[CHANNEL], radio->registers[SEEKCFG1]);
			retval = rtc6213n_set_register(radio, CHANNEL);
			break;
		case V4L2_CID_PRIVATE_CSR0_CHSPACE:
			radio->registers[CHANNEL] &= ~CHANNEL_CSR0_CHSPACE;
			radio->registers[CHANNEL] |= (ctrl->value << 10);
			retval = rtc6213n_set_register(radio, CHANNEL);
			break;
		case V4L2_CID_PRIVATE_CSR0_DIS_AGC:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_DIS_AGC val=%d\n",
					ctrl->value);
			break;
		case V4L2_CID_PRIVATE_CSR0_RDS_EN:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_RDS_EN : CHANNEL=0x%4.4hx SYSCFG=0x%4.4hx\n",
					radio->registers[CHANNEL], radio->registers[SYSCFG]);
			rtc6213n_reset_rds_data(radio);
			radio->registers[SYSCFG] &= ~SYSCFG_CSR0_RDS_EN;
			radio->registers[SYSCFG] &= ~SYSCFG_CSR0_RDSIRQEN;
			radio->registers[SYSCFG] |= (ctrl->value << 15);
			radio->registers[SYSCFG] |= (ctrl->value << 12);
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_RDS_EN : CHANNEL=0x%4.4hx SYSCFG=0x%4.4hx\n",
					radio->registers[CHANNEL], radio->registers[SYSCFG]);
			retval = rtc6213n_set_register(radio, SYSCFG);
			break;
		case V4L2_CID_PRIVATE_SEEK_CANCEL:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_SEEK_CANCEL : MPXCFG=0x%4.4hx SEEKCFG1=0x%4.4hx\n",
					radio->registers[MPXCFG], radio->registers[SEEKCFG1]);
			if (rtc6213n_wq_flag == SEEK_WAITING) {
				rtc6213n_wq_flag = SEEK_CANCEL;
				wake_up_interruptible(&rtc6213n_wq);
				dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_SEEK_CANCEL : sent SEEK_CANCEL signal\n");
			}
			/* Make sure next seek_cancel is workable */
			ctrl->value = 0;
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_SEEK_CANCEL : MPXCFG=0x%4.4hx SEEKCFG1=0x%4.4hx\n",
					radio->registers[MPXCFG], radio->registers[SEEKCFG1]);
			break;
		case V4L2_CID_PRIVATE_CSR0_SEEKRSSITH:
			radio->registers[SEEKCFG1] &= ~SEEKCFG1_CSR0_SEEKRSSITH;
			radio->registers[SEEKCFG1] |= ctrl->value;
			retval = rtc6213n_set_register(radio, SEEKCFG1);
			break;
		case V4L2_CID_PRIVATE_CSR0_OFSTH:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_OFSTH : SEEKCFG2=0x%4.4hx\n",
					radio->registers[SEEKCFG2]);
			radio->registers[SEEKCFG2] &= ~SEEKCFG2_CSR0_OFSTH;
			radio->registers[SEEKCFG2] |= (ctrl->value << 8);

			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_OFSTH : SEEKCFG2=0x%4.4hx\n",
					radio->registers[SEEKCFG2]);
			retval = rtc6213n_set_register(radio, SEEKCFG2);
			break;
		case V4L2_CID_PRIVATE_CSR0_QLTTH:
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_QLTTH : SEEKCFG2=0x%4.4hx\n",
					radio->registers[SEEKCFG2]);
			radio->registers[SEEKCFG2] &= ~SEEKCFG2_CSR0_QLTTH;
			radio->registers[SEEKCFG2] |= ctrl->value;
			dev_info(&radio->videodev.dev, "V4L2_CID_PRIVATE_CSR0_QLTTH : SEEKCFG2=0x%4.4hx\n",
					radio->registers[SEEKCFG2]);
			retval = rtc6213n_set_register(radio, SEEKCFG2);
			break;
		default:
			pr_info("%s id: %x in default\n",__func__, ctrl->id);
			retval = -EINVAL;
			break;
	}

end:
	pr_info("%s exit id: %x , ret: %d\n",__func__, ctrl->id, retval);

	return retval;
}

/*
 * rtc6213n_vidioc_g_audio - get audio attributes
 */
static int rtc6213n_vidioc_g_audio(struct file *file, void *priv,
	struct v4l2_audio *audio)
{
	/* driver constants */
	audio->index = 0;
	strcpy(audio->name, "Radio");
	audio->capability = V4L2_AUDCAP_STEREO;
	audio->mode = 0;

	return 0;
}


/*
 * rtc6213n_vidioc_g_tuner - get tuner attributes
 */
static int rtc6213n_vidioc_g_tuner(struct file *file, void *priv,
	struct v4l2_tuner *tuner)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	pr_info("%s enter\n", __func__);

	if (tuner->index != 0) {
		retval = -EINVAL;
		goto done;
	}

	retval = rtc6213n_get_register(radio, RSSI);
	if (retval < 0)
		goto done;

	/* driver constants */
	strcpy(tuner->name, "FM");
	tuner->type = V4L2_TUNER_RADIO;
	tuner->capability = V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO |
		V4L2_TUNER_CAP_RDS | V4L2_TUNER_CAP_RDS_BLOCK_IO;

	/* range limits */
	switch ((radio->registers[CHANNEL] & CHANNEL_CSR0_BAND) >> 12) {
	/* 0: 87.5 - 108 MHz (USA, Europe, default) */
	default:
		tuner->rangelow  =  87.5 * FREQ_MUL;
		tuner->rangehigh = 108   * FREQ_MUL;
	break;
	/* 1: 76   - 108 MHz (Japan wide band) */
	case 1:
		tuner->rangelow  =  76   * FREQ_MUL;
		tuner->rangehigh = 108   * FREQ_MUL;
	break;
	/* 2: 76   -  90 MHz (Japan) */
	case 2:
		tuner->rangelow  =  76   * FREQ_MUL;
		tuner->rangehigh =  90   * FREQ_MUL;
	break;
	};

	/* stereo indicator == stereo (instead of mono) */
	if ((radio->registers[STATUS] & STATUS_SI) == 0)
		tuner->rxsubchans = V4L2_TUNER_SUB_MONO;
	else
		tuner->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	/* If there is a reliable method of detecting an RDS channel,
	   then this code should check for that before setting this
	   RDS subchannel. */
	tuner->rxsubchans |= V4L2_TUNER_SUB_RDS;

	/* mono/stereo selector */
	if ((radio->registers[MPXCFG] & MPXCFG_CSR0_MONO) == 0)
		tuner->audmode = V4L2_TUNER_MODE_STEREO;
	else
		tuner->audmode = V4L2_TUNER_MODE_MONO;

	/* min is worst, max is best; rssi: 0..0xff */
	tuner->signal = (radio->registers[RSSI] & RSSI_RSSI);

done:
	pr_info("%s exit %d\n",	__func__, retval);

	return retval;
}


/*
 * rtc6213n_vidioc_s_tuner - set tuner attributes
 */
static int rtc6213n_vidioc_s_tuner(struct file *file, void *priv,
	const struct v4l2_tuner *tuner)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	pr_info("%s entry\n", __func__);

	if (tuner->index != 0){
		pr_info("%s index :%d\n",__func__, tuner->index);
		goto done;
	}

	/* mono/stereo selector */
	switch (tuner->audmode) {
	case V4L2_TUNER_MODE_MONO:
		radio->registers[MPXCFG] |= MPXCFG_CSR0_MONO;  /* force mono */
		break;
	case V4L2_TUNER_MODE_STEREO:
		radio->registers[MPXCFG] &= ~MPXCFG_CSR0_MONO; /* try stereo */
		break;
	default:
		goto done;
	}

	retval = rtc6213n_set_register(radio, MPXCFG);

	pr_info("%s low:%d high:%d\n", __func__, tuner->rangelow, tuner->rangehigh);

	/* set band */
	if (tuner->rangelow || tuner->rangehigh) {
		for (band = 0; band < ARRAY_SIZE(bands); band++) {
			if (bands[band].rangelow  == tuner->rangelow &&
					bands[band].rangehigh == tuner->rangehigh)
				break;
		}
		if (band == ARRAY_SIZE(bands)){
			pr_err("%s err\n",__func__);
			band = 0;
		}
	} else
		band = 0; /* If nothing is specified seek 87.5 - 108 Mhz */

	if (radio->band != band) {
		radio->registers[CHANNEL] |= (band  << 12);
		rtc6213n_set_register(radio, MPXCFG);
		radio->band = band;
	}
done:
	pr_info("%s exit %d\n",__func__, retval);
	return retval;
}


/*
 * rtc6213n_vidioc_g_frequency - get tuner or modulator radio frequency
 */
static int rtc6213n_vidioc_g_frequency(struct file *file, void *priv,
	struct v4l2_frequency *freq)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	pr_info("%s enter freq %d\n", __func__, freq->frequency);

	freq->type = V4L2_TUNER_RADIO;
	retval = rtc6213n_get_freq(radio, &freq->frequency);
	pr_info(" %s *freq=%d, ret %d\n",__func__, freq->frequency, retval);

	if (retval < 0)
		pr_err(" %s get frequency failed with %d\n", __func__, retval);

	return retval;
}


/*
 * rtc6213n_vidioc_s_frequency - set tuner or modulator radio frequency
 */
static int rtc6213n_vidioc_s_frequency(struct file *file, void *priv,
	const struct v4l2_frequency *freq)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	pr_info("%s enter freq = %d\n", __func__ ,freq->frequency);

	retval = rtc6213n_set_freq(radio, freq->frequency);

	if (retval < 0)
		pr_err("%s set frequency failed with %d\n", __func__, retval);

	return retval;
}


/*
 * rtc6213n_vidioc_s_hw_freq_seek - set hardware frequency seek
 */
static int rtc6213n_vidioc_s_hw_freq_seek(struct file *file, void *priv,
	const struct v4l2_hw_freq_seek *seek)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = 0;

	pr_info("%s enter\n", __func__);

	if (file->f_flags & O_NONBLOCK)
		return -EWOULDBLOCK;

	radio->is_search_cancelled = false;

	if (radio->g_search_mode == SEEK) {
		/* seek */
		pr_info("%s starting seek\n",__func__);
		radio->seek_tune_status = SEEK_PENDING;
		retval = rtc6213n_set_seek(radio, seek->seek_upward, seek->wrap_around);
		rtc6213n_q_event(radio, RTC6213N_EVT_TUNE_SUCC);
		radio->seek_tune_status = NO_SEEK_TUNE_PENDING;
		rtc6213n_q_event(radio, RTC6213N_EVT_SCAN_NEXT);
	} else if ((radio->g_search_mode == SCAN) ||
			(radio->g_search_mode == SCAN_FOR_STRONG)) {
		/* scan */
		if (radio->g_search_mode == SCAN_FOR_STRONG) {
			pr_info("%s starting search list\n",__func__);
			memset(&radio->srch_list, 0,
					sizeof(struct rtc6213n_srch_list_compl));
		} else {
			pr_info("%s starting scan\n",__func__);
		}
		rtc6213n_search(radio, 1);
	} else {
		retval = -EINVAL;
		pr_err("In %s, invalid search mode %d\n",
				__func__, radio->g_search_mode);
	}
	pr_info("%s exit %d\n", __func__, retval);
	return retval;
}

static int rtc6213n_vidioc_g_fmt_type_private(struct file *file, void *priv,
						struct v4l2_format *f)
{
	return 0;
}

#if 0
/* v4l2_subdev_ops v4l2_ctrl_ops v4l2_ioctl_ops was addpted*/
const struct v4l2_ctrl_ops rtc6213n_ctrl_ops = {
	.g_volatile_ctrl            =   rtc6213n_vidioc_g_ctrl,
	.s_ctrl                     =   rtc6213n_vidioc_s_ctrl,
};
#endif

static const struct v4l2_file_operations rtc6213n_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl32	= v4l2_compat_ioctl32,
#endif
	.read			= rtc6213n_fops_read,
	.poll			= rtc6213n_fops_poll,
	.open			= rtc6213n_fops_open,
	.release		= rtc6213n_fops_release,
};

/*
 * rtc6213n_ioctl_ops - video device ioctl operations
 */
/* static */
const struct v4l2_ioctl_ops rtc6213n_ioctl_ops = {
	.vidioc_querycap            =   rtc6213n_vidioc_querycap,
	.vidioc_g_audio             =   rtc6213n_vidioc_g_audio,
	.vidioc_g_tuner             =   rtc6213n_vidioc_g_tuner,
	.vidioc_s_tuner             =   rtc6213n_vidioc_s_tuner,
	.vidioc_g_ctrl				= 	rtc6213n_vidioc_g_ctrl,
	.vidioc_s_ctrl 				= 	rtc6213n_vidioc_s_ctrl,
	.vidioc_g_frequency         =   rtc6213n_vidioc_g_frequency,
	.vidioc_s_frequency         =   rtc6213n_vidioc_s_frequency,
	.vidioc_s_hw_freq_seek      =   rtc6213n_vidioc_s_hw_freq_seek,
	.vidioc_dqbuf       		= 	rtc6213n_vidioc_dqbuf,
	.vidioc_g_fmt_type_private	= 	rtc6213n_vidioc_g_fmt_type_private,
};

/*
 * rtc6213n_viddev_template - video device interface
 */
struct video_device rtc6213n_viddev_template = {
	.fops           =   &rtc6213n_fops,
	.name           =   DRIVER_NAME,
	.release        =   video_device_release_empty,
	.ioctl_ops      =   &rtc6213n_ioctl_ops,
};

/**************************************************************************
 * Module Interface
 **************************************************************************/

/*
 * rtc6213n_i2c_init - module init
 */
static __init int rtc6213n_init(void)
{
	pr_info(KERN_INFO DRIVER_DESC ", Version " DRIVER_VERSION "\n");
	return rtc6213n_i2c_init();
}

/*
 * rtc6213n_i2c_exit - module exit
 */
static void __exit rtc6213n_exit(void)
{
	i2c_del_driver(&rtc6213n_i2c_driver);
}

module_init(rtc6213n_init);
module_exit(rtc6213n_exit);
