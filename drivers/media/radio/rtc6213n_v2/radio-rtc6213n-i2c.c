/*
 * drivers/media/radio/rtc6213n/radio-rtc6213n-i2c.c
 *
 * I2C driver for Richwave RTC6213N FM Tuner
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

/* kernel includes */
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include "radio-rtc6213n.h"
#include <linux/workqueue.h>

static struct of_device_id rtc6213n_i2c_dt_ids[] = {
	{.compatible = "rtc6213n"},
	{}
};

/* I2C Device ID List */
static const struct i2c_device_id rtc6213n_i2c_id[] = {
    /* Generic Entry */
	{ "rtc6213n", 0 },
	/* Terminating entry */
	{ }
};
MODULE_DEVICE_TABLE(i2c, rtc6213n_i2c_id);


/**************************************************************************
 * Module Parameters
 **************************************************************************/

/* Radio Nr */
static int radio_nr = -1;
module_param(radio_nr, int, 0444);
MODULE_PARM_DESC(radio_nr, "Radio Nr");

/* RDS buffer blocks */
static unsigned int rds_buf = 100;
module_param(rds_buf, uint, 0444);
MODULE_PARM_DESC(rds_buf, "RDS buffer entries: *100*");

/* RDS maximum block errors */
static unsigned short max_rds_errors = 1;
/* 0 means   0  errors requiring correction */
/* 1 means 1-2  errors requiring correction */
/* 2 means 3-5  errors requiring correction */
/* 3 means   6+ errors or errors in checkword, correction not possible */
module_param(max_rds_errors, ushort, 0644);
MODULE_PARM_DESC(max_rds_errors, "RDS maximum block errors: *1*");

enum rtc6213n_ctrl_id {
	RTC6213N_ID_CSR0_ENABLE,
	RTC6213N_ID_CSR0_DISABLE,
	RTC6213N_ID_DEVICEID,
	RTC6213N_ID_CSR0_DIS_SMUTE,
	RTC6213N_ID_CSR0_DIS_MUTE,
	RTC6213N_ID_CSR0_DEEM,
	RTC6213N_ID_CSR0_BLNDADJUST,
	RTC6213N_ID_CSR0_VOLUME,
	RTC6213N_ID_CSR0_BAND,
	RTC6213N_ID_CSR0_CHSPACE,
	RTC6213N_ID_CSR0_DIS_AGC,
	RTC6213N_ID_CSR0_RDS_EN,
	RTC6213N_ID_SEEK_CANCEL,
	RTC6213N_ID_CSR0_SEEKRSSITH,
	RTC6213N_ID_CSR0_OFSTH,
	RTC6213N_ID_CSR0_QLTTH,
	RTC6213N_ID_RSSI,
	RTC6213N_ID_RDS_RDY,
	RTC6213N_ID_STD,
	RTC6213N_ID_SF,
	RTC6213N_ID_RDS_SYNC,
	RTC6213N_ID_SI,
};

#if 0
static struct v4l2_ctrl_config rtc6213n_ctrls[] = {
	[RTC6213N_ID_CSR0_ENABLE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_ENABLE,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_ENABLE",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_DISABLE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DISABLE,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DISABLE",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_DEVICEID] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_DEVICEID,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "DEVICEID",
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_DIS_SMUTE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DIS_SMUTE,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_DIS_SMUTE",
		.min    = 0,
		.max    = 1,
		.step   = 1,
	},
	[RTC6213N_ID_CSR0_DIS_MUTE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DIS_MUTE,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DIS_MUTE",
		.min    = 0,
		.max    = 1,
		.step   = 1,
	},
	[RTC6213N_ID_CSR0_DEEM] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DEEM,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DEEM",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_BLNDADJUST] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_BLNDADJUST,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_BLNDADJUST",
		.min	= 0,
		.max	= 15,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_VOLUME] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_VOLUME,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_VOLUME",
		.min    = 0,
		.max    = 15,
		.step   = 1,
	},
	[RTC6213N_ID_CSR0_BAND] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_BAND,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_BAND",
		.min	= 0,
		.max	= 3,
		.step	= 1,
		},
	[RTC6213N_ID_CSR0_CHSPACE] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_CHSPACE,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_CHSPACE",
		.min	= 0,
		.max	= 3,
		.step	= 1,
		},
	[RTC6213N_ID_CSR0_DIS_AGC] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_DIS_AGC,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_DIS_AGC",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_RDS_EN] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_RDS_EN,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "CSR0_RDS_EN",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_SEEK_CANCEL] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_SEEK_CANCEL,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "SEEK_CANCEL",
		.min	= 0,
		.max	= 1,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_SEEKRSSITH] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_SEEKRSSITH,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_SEEKRSSITH",
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_OFSTH] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_OFSTH,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_OFSTH",
		.def    = 64,
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_CSR0_QLTTH] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_CSR0_QLTTH,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_QLTTH",
		.def    = 80,
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
	[RTC6213N_ID_RSSI] = {
		.ops	= &rtc6213n_ctrl_ops,
		.id		= V4L2_CID_PRIVATE_RSSI,
		.type	= V4L2_CTRL_TYPE_INTEGER,
		.name	= "CSR0_SEEKRSSITH",
		.flags  = V4L2_CTRL_FLAG_VOLATILE,
		.min	= 0,
		.max	= 255,
		.step	= 1,
	},
};
#endif

/*static*/
struct tasklet_struct my_tasklet;
/*
 * rtc6213n_get_register - read register
 */
int rtc6213n_get_register(struct rtc6213n_device *radio, int regnr)
{
	u16 buf[READ_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	radio->registers[regnr] = __be16_to_cpu(buf[READ_INDEX(regnr)]);
	return 0;
}

/*
 * rtc6213n_set_register - write register
 */
int rtc6213n_set_register(struct rtc6213n_device *radio, int regnr)
{
	int i;
	u16 buf[WRITE_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, 0, sizeof(u16) * WRITE_REG_NUM,
			(void *)buf },
	};

	for (i = 0; i < WRITE_REG_NUM; i++)
		buf[i] = __cpu_to_be16(radio->registers[WRITE_INDEX(i)]);

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	return 0;
}

/*
 * rtc6213n_set_register - write register
 */
int rtc6213n_set_serial_registers(struct rtc6213n_device *radio,
	u16 *data, int bytes)
{
	int i;
	u16 buf[46];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, 0, sizeof(u16) * bytes,
			(void *)buf },
	};

	for (i = 0; i < bytes; i++)
		buf[i] = __cpu_to_be16(data[i]);

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	return 0;
}

/*
 * rtc6213n_get_all_registers - read entire registers
 */
/* changed from static */
int rtc6213n_get_all_registers(struct rtc6213n_device *radio)
{
	int i;
	u16 buf[READ_REG_NUM];
	struct i2c_msg msgs[1] = {
		{ radio->client->addr, I2C_M_RD, sizeof(u16) * READ_REG_NUM,
			(void *)buf },
	};

	if (i2c_transfer(radio->client->adapter, msgs, 1) != 1)
		return -EIO;

	for (i = 0; i < READ_REG_NUM; i++)
		radio->registers[i] = __be16_to_cpu(buf[READ_INDEX(i)]);

	return 0;
}

/*
 * rtc6213n_vidioc_querycap - query device capabilities
 */
int rtc6213n_vidioc_querycap(struct file *file, void *priv,
	struct v4l2_capability *capability)
{
	pr_info("%s enter\n", __func__);
	strlcpy(capability->driver, DRIVER_NAME, sizeof(capability->driver));
	strlcpy(capability->card, DRIVER_CARD, sizeof(capability->card));
	capability->device_caps = V4L2_CAP_HW_FREQ_SEEK | V4L2_CAP_READWRITE |
		V4L2_CAP_TUNER | V4L2_CAP_RADIO | V4L2_CAP_RDS_CAPTURE;
	capability->capabilities = capability->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

/*
 * rtc6213n_i2c_interrupt - interrupt handler
 */
static void rtc6213n_i2c_interrupt_handler(struct rtc6213n_device *radio)
{
	unsigned char regnr;
	int retval = 0;

	pr_info("%s enter\n", __func__);

	/* check Seek/Tune Complete */
	retval = rtc6213n_get_register(radio, STATUS);
	if (retval < 0){
		pr_err("%s read fail to STATUS\n", __func__);
		goto end;
	}
	retval = rtc6213n_get_register(radio, RSSI);
	if (retval < 0){
		pr_err("%s read fail to RSSI\n", __func__);
		goto end;
	}

	if (radio->registers[STATUS] & STATUS_STD) {
		complete(&radio->completion);
		pr_info("%s Seek/Tune Done\n",__func__);
	}

	/* Update RDS registers */
	for (regnr = 1; regnr < RDS_REGISTER_NUM; regnr++) {
		retval = rtc6213n_get_register(radio, STATUS + regnr);
		if (retval < 0)
			goto end;
	}

	/* get rds blocks */
	if ((radio->registers[STATUS] & STATUS_RDS_RDY) == 0){
		/* No RDS group ready, better luck next time */
		pr_err("%s No RDS group ready\n",__func__);
		goto end;
	} else {
		pr_info("%s start rds handler\n",__func__);
		schedule_work(&radio->rds_worker);
	}
end:
	pr_info("%s exit :%d\n",__func__, retval);
	return;
}

static irqreturn_t rtc6213n_isr(int irq, void *dev_id)
{
	struct rtc6213n_device *radio = dev_id;
	/*
	 * The call to queue_delayed_work ensures that a minimum delay
	 * (in jiffies) passes before the work is actually executed. The return
	 * value from the function is nonzero if the work_struct was actually
	 * added to queue (otherwise, it may have already been there and will
	 * not be added a second time).
	 */

	queue_delayed_work(radio->wqueue, &radio->work,
				msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

static void rtc6213n_handler(struct work_struct *work)
{
	struct rtc6213n_device *radio;

	radio = container_of(work, struct rtc6213n_device, work.work);

	rtc6213n_i2c_interrupt_handler(radio);
}

void rtc6213n_disable_irq(struct rtc6213n_device *radio)
{
	int irq;

	irq = radio->irq;
	disable_irq_wake(irq);
	free_irq(irq, radio);

	cancel_delayed_work_sync(&radio->work);
	flush_workqueue(radio->wqueue);

	cancel_work_sync(&radio->rds_worker);
	flush_workqueue(radio->wqueue_rds);
	cancel_delayed_work_sync(&radio->work_scan);
	flush_workqueue(radio->wqueue_scan);
}

int rtc6213n_enable_irq(struct rtc6213n_device *radio)
{
	int retval;
	int irq;

	retval = gpio_direction_input(radio->int_gpio);
	if (retval) {
		pr_err("%s unable to set the gpio %d direction(%d)\n",
				__func__,radio->int_gpio, retval);
		return retval;
	}
	radio->irq = gpio_to_irq(radio->int_gpio);
	irq = radio->irq;

	if (radio->irq < 0) {
		pr_err("%s: gpio_to_irq returned %d\n", __func__, radio->irq);
		goto open_err_req_irq;
	}

	pr_info("%s irq number is = %d\n", __func__,radio->irq);

	retval = request_any_context_irq(radio->irq, rtc6213n_isr, IRQF_TRIGGER_FALLING, DRIVER_NAME, radio);

	if (retval < 0) {
		pr_err("%s Couldn't acquire FM gpio %d, retval:%d\n", __func__,radio->irq,retval);
		return retval;
	} else {
		pr_info("%s FM GPIO %d registered\n", __func__, radio->irq);
	}
	retval = enable_irq_wake(irq);
	if (retval < 0) {
		pr_err("Could not wake FM interrupt\n ");
		free_irq(irq , radio);
	}
	return retval;

open_err_req_irq:
	rtc6213n_disable_irq(radio);

	return retval;
}

/*
 * rtc6213n_fops_open - file open
 */
int rtc6213n_fops_open(struct file *file)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = v4l2_fh_open(file);
	int i;

	pr_info("%s enter\n", __func__);

	INIT_DELAYED_WORK(&radio->work, rtc6213n_handler);
	INIT_DELAYED_WORK(&radio->work_scan, rtc6213n_scan);
	INIT_WORK(&radio->rds_worker, rtc6213n_rds_handler);

	retval = rtc6213n_power_up(radio);
	if(retval < 0){
		pr_err("%s fail to power_up\n", __func__);
		return retval;
	}
	retval = rtc6213n_get_all_registers(radio);
	if(retval < 0)
	 pr_err("%s fail to get register %d\n", __func__, retval);
	else{
		for(i = 0; i < 16; i++ )
			pr_info("%s registers[%d]:%x\n", __func__, i,radio->registers[i]);
	}
	/* Wait for the value to take effect on gpio. */
	msleep(100);

	rtc6213n_enable_irq(radio);
	return retval;
}

/*
 * rtc6213n_fops_release - file release
 */
int rtc6213n_fops_release(struct file *file)
{
	struct rtc6213n_device *radio = video_drvdata(file);
	int retval = v4l2_fh_release(file);

	dev_info(&radio->videodev.dev, "rtc6213n_fops_release : Exit\n");
	rtc6213n_power_down(radio);
	if(retval < 0){
		pr_err("%s fail to power_down\n", __func__);
		return retval;
	}

	return retval;
}

static int rtc6213n_parse_dt(struct device *dev,
			struct rtc6213n_device *radio)
{
	int rc = 0;
	struct device_node *np = dev->of_node;

	radio->int_gpio = of_get_named_gpio(np, "fmint-gpio", 0);
	if (radio->int_gpio < 0) {
		pr_err("%s int-gpio not provided in device tree",__func__);
		rc = radio->int_gpio;
		goto err_int_gpio;
	}

	rc = gpio_request(radio->int_gpio, "fm_int");
	if (rc) {
		pr_err("%s unable to request gpio %d (%d)\n",__func__,
						radio->int_gpio, rc);
		goto err_int_gpio;
	}

	rc = gpio_direction_output(radio->int_gpio, 0);
	if (rc) {
		pr_err("%s unable to set the gpio %d direction(%d)\n",
		__func__,radio->int_gpio , rc);
		goto err_int_gpio;
	}
		/* Wait for the value to take effect on gpio. */
	msleep(100);

	return rc;

err_int_gpio:
	gpio_free(radio->int_gpio);

	return rc;
}

/*
 * rtc6213n_i2c_probe - probe for the device
 */
static int rtc6213n_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct rtc6213n_device *radio;
	struct v4l2_device *v4l2_dev;
	struct v4l2_ctrl_handler *hdl;
	int retval = 0;
	int i = 0;
	int kfifo_alloc_rc = 0;

	/* struct v4l2_ctrl *ctrl; */
	/* need to add description "irq-fm" in dts */

    pr_info("%s enter\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		retval = -ENODEV;
		goto err_initial;
	}

	/* private data allocation and initialization */
	radio = kzalloc(sizeof(struct rtc6213n_device), GFP_KERNEL);
	if (!radio) {
		retval = -ENOMEM;
		goto err_initial;
	}

	v4l2_dev = &radio->v4l2_dev;
	retval = v4l2_device_register(&client->dev, v4l2_dev);
	if (retval < 0) {
		dev_err(&client->dev, "couldn't register v4l2_device\n");
		goto err_radio;
	}

	dev_info(&client->dev, "v4l2_device_register successfully\n");
	hdl = &radio->ctrl_handler;

	radio->users = 0;
	radio->client = client;
	mutex_init(&radio->lock);
	init_completion(&radio->completion);

	retval = rtc6213n_parse_dt(&client->dev, radio);
	if (retval) {
		pr_err("%s: Parsing DT failed(%d)", __func__, retval);
		kfree(radio);
		return retval;
	}

	memcpy(&radio->videodev, &rtc6213n_viddev_template,
		sizeof(struct video_device));

	radio->videodev.v4l2_dev = v4l2_dev;
	radio->videodev.ioctl_ops = &rtc6213n_ioctl_ops;
	video_set_drvdata(&radio->videodev, radio);

	/* rds buffer allocation */
	radio->buf_size = rds_buf * 3;
	radio->buffer = kmalloc(radio->buf_size, GFP_KERNEL);
	if (!radio->buffer) {
		retval = -EIO;
		goto err_radio;
	}

	for (i = 0; i < RTC6213N_FM_BUF_MAX; i++) {
		spin_lock_init(&radio->buf_lock[i]);

		kfifo_alloc_rc = kfifo_alloc(&radio->data_buf[i],
				STD_BUF_SIZE, GFP_KERNEL);

		if (kfifo_alloc_rc != 0) {
			pr_err("%s: failed allocating buffers %d\n",
					__func__, kfifo_alloc_rc);
			retval = -ENOMEM;
			goto err;
		}
	}
	radio->wqueue = NULL;
	radio->wqueue_scan = NULL;
	radio->wqueue_rds = NULL;

	/* rds buffer configuration */
	radio->wr_index = 0;
	radio->rd_index = 0;
	init_waitqueue_head(&radio->event_queue);
	init_waitqueue_head(&radio->read_queue);
	init_waitqueue_head(&rtc6213n_wq);

	radio->wqueue  = create_singlethread_workqueue("fmradio");
	if (!radio->wqueue) {
		retval = -ENOMEM;
		goto err_wqueue;
	}

	radio->wqueue_scan  = create_singlethread_workqueue("fmradioscan");
	if (!radio->wqueue_scan) {
		retval = -ENOMEM;
		goto err_wqueue_scan;
	}

	radio->wqueue_rds  = create_singlethread_workqueue("fmradiords");
	if (!radio->wqueue_rds) {
		retval = -ENOMEM;
		goto err_all;
	}

	/* register video device */
	retval = video_register_device(&radio->videodev, VFL_TYPE_RADIO,
		radio_nr);
	if (retval) {
		dev_info(&client->dev, "Could not register video device\n");
		goto err_rds;
	}

	i2c_set_clientdata(client, radio);		/* move from below */
	pr_info("%s exit\n", __func__);
	return 0;

err_all:
	destroy_workqueue(radio->wqueue_rds);
err_wqueue_scan:
	destroy_workqueue(radio->wqueue_scan);
err_rds:
	kfree(radio->buffer);
err_wqueue:
	destroy_workqueue(radio->wqueue);
err:
	video_device_release(&radio->videodev);
	v4l2_device_unregister(v4l2_dev);
err_radio:
	kfree(radio);
err_initial:
	return retval;
}

/*
 * rtc6213n_i2c_remove - remove the device
 */
static int rtc6213n_i2c_remove(struct i2c_client *client)
{
	struct rtc6213n_device *radio = i2c_get_clientdata(client);

	free_irq(client->irq, radio);
	kfree(radio->buffer);
	video_device_release(&radio->videodev);
	v4l2_ctrl_handler_free(&radio->ctrl_handler);
	video_unregister_device(&radio->videodev);
	v4l2_device_unregister(&radio->v4l2_dev);
	kfree(radio);
	dev_info(&client->dev, "rtc6213n_i2c_remove exit\n");

	return 0;
}

#ifdef CONFIG_PM
/*
 * rtc6213n_i2c_suspend - suspend the device
 */
static int rtc6213n_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rtc6213n_device *radio = i2c_get_clientdata(client);

	dev_info(&radio->videodev.dev, "rtc6213n_i2c_suspend\n");
	return 0;
}


/*
 * rtc6213n_i2c_resume - resume the device
 */
static int rtc6213n_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rtc6213n_device *radio = i2c_get_clientdata(client);

	dev_info(&radio->videodev.dev, "rtc6213n_i2c_resume\n");

	return 0;
}

static SIMPLE_DEV_PM_OPS(rtc6213n_i2c_pm, rtc6213n_i2c_suspend,
						rtc6213n_i2c_resume);
#endif


/*
 * rtc6213n_i2c_driver - i2c driver interface
 */
struct i2c_driver rtc6213n_i2c_driver = {
	.driver = {
		.name			= "rtc6213n",
		.owner			= THIS_MODULE,
		.of_match_table = of_match_ptr(rtc6213n_i2c_dt_ids),
#ifdef CONFIG_PM
		.pm				= &rtc6213n_i2c_pm,
#endif
	},
	.probe				= rtc6213n_i2c_probe,
	.remove				= rtc6213n_i2c_remove,
	.id_table			= rtc6213n_i2c_id,
};

/*
 * rtc6213n_i2c_init
 */
int rtc6213n_i2c_init(void)
{
	pr_info(KERN_INFO DRIVER_DESC ", Version " DRIVER_VERSION "\n");
	return i2c_add_driver(&rtc6213n_i2c_driver);
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
