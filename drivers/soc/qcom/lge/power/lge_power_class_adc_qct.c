/*
 *  Copyright (C) 2014, Daeho Choi <daeho.choi@lge.com>
 *  Driver for lg adc
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 */
#define pr_fmt(fmt) "[LGE-ADC] %s : " fmt, __func__

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <soc/qcom/lge/power/lge_power_class.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/wakelock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#ifdef CONFIG_MACH_MSM8996_LUCYE
#include <linux/delay.h>
#include <linux/spmi.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <soc/qcom/lge/power/lge_board_revision.h>
#endif

#define DEFAULT_TEMP		250

struct lge_adc {
	struct device 				*dev;
	struct lge_power 			lge_adc_lpc;
	struct lge_power 			*lge_cc_lpc;
	struct power_supply 		*fuelgauge_psy;
	struct power_supply 		*ac_psy;
	struct power_supply 		*battery_psy;
	struct power_supply 		*usb_psy;
	struct power_supply 		*bms_psy;
	struct power_supply 		*wlc_psy;
	struct qpnp_vadc_chip 		*pm_vadc;
	struct qpnp_vadc_chip 		*pmi_vadc;
	uint32_t 		xo_therm_channel;
	uint32_t 		pa0_therm_channel;
	uint32_t 		pa1_therm_channel;
	uint32_t 		bd1_therm_channel;
	uint32_t 		bd2_therm_channel;
	uint32_t 		batt_therm_channel;
	uint32_t 		usb_id_channel;
	int 			status;
#ifdef CONFIG_MACH_MSM8996_LUCYE
	struct mutex lock_for_usb_id;
	signed sbu_en;
#endif
};

static enum lge_power_property lge_power_adc_properties[] = {
	LGE_POWER_PROP_STATUS,
	LGE_POWER_PROP_XO_THERM_PHY,
	LGE_POWER_PROP_XO_THERM_RAW,
	LGE_POWER_PROP_BATT_THERM_PHY,
	LGE_POWER_PROP_BATT_THERM_RAW,
	LGE_POWER_PROP_USB_ID_PHY,
	LGE_POWER_PROP_USB_ID_RAW,
	LGE_POWER_PROP_PA0_THERM_PHY,
	LGE_POWER_PROP_PA0_THERM_RAW,
	LGE_POWER_PROP_PA1_THERM_PHY,
	LGE_POWER_PROP_PA1_THERM_RAW,
	LGE_POWER_PROP_BD1_THERM_PHY,
	LGE_POWER_PROP_BD1_THERM_RAW,
	LGE_POWER_PROP_BD2_THERM_PHY,
	LGE_POWER_PROP_BD2_THERM_RAW,
	LGE_POWER_PROP_TEMP,
};

static int lge_power_adc_get_property(struct lge_power *lpc,
				enum lge_power_property lpp,
				union lge_power_propval *val)
{
	int ret_val = 0;

	struct qpnp_vadc_result results;
	int rc = 0;
	struct lge_adc *lge_adc_chip
			= container_of(lpc, struct lge_adc, lge_adc_lpc);
	union power_supply_propval prop = {0, };

#ifdef CONFIG_MACH_MSM8996_LUCYE
	struct power_supply *usb_pd_psy;
	u8 data;
	int waterproof = 0, typec_accessory = 0;
#endif

	switch (lpp) {
	case LGE_POWER_PROP_STATUS:
		val->intval = lge_adc_chip->status;
		break;

	case LGE_POWER_PROP_XO_THERM_PHY:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->xo_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->xo_therm_channel, &results);
			} else {
				pr_err("VADC is not used for XO_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read xo temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_XO_THERM_RAW:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->xo_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
				lge_adc_chip->xo_therm_channel, &results);
			} else {
				pr_err("VADC is not used for XO_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read xo temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_BATT_THERM_PHY:
#ifdef CONFIG_ARCH_MSM8996
	rc = lge_adc_chip->bms_psy->get_property(lge_adc_chip->bms_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
	val->int64val = prop.intval;
#else
		if (!IS_ERR(lge_adc_chip->pmi_vadc)) {
			if (lge_adc_chip->batt_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pmi_vadc,
					lge_adc_chip->batt_therm_channel, &results);
			} else {
				pr_err("VADC is not used for BATT_THERM!!!\n");
				lge_adc_chip->bms_psy = power_supply_get_by_name("bms");
				if (lge_adc_chip->bms_psy == NULL) {
					pr_err("Failed to get bms property\n");
					rc = -1;
				} else {
					rc = lge_adc_chip->bms_psy->get_property(lge_adc_chip->bms_psy,
						POWER_SUPPLY_PROP_TEMP, &prop);
					if (rc) {
						pr_err("Failed to get temp from property\n");
						rc = -1;
					}

					val->intval = prop.intval;
				}
			}
			if (rc) {
				pr_err("Unable to read batt temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			val->intval = 0;
		}
#endif
		break;

	case LGE_POWER_PROP_BATT_THERM_RAW:
#ifdef CONFIG_ARCH_MSM8996
	rc = lge_adc_chip->bms_psy->get_property(lge_adc_chip->bms_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
	val->intval = prop.intval;
#else
		if (!IS_ERR(lge_adc_chip->pmi_vadc)) {
			if (lge_adc_chip->batt_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pmi_vadc,
					lge_adc_chip->batt_therm_channel, &results);
			} else {
				pr_err("VADC is not used for BATT_THERM!!!\n");

				lge_adc_chip->bms_psy = power_supply_get_by_name("bms");
				if (lge_adc_chip->bms_psy == NULL) {
					pr_err("Failed to get bms property\n");
					rc = -1;
				} else {
					rc = lge_adc_chip->bms_psy->get_property(lge_adc_chip->bms_psy,
						POWER_SUPPLY_PROP_TEMP, &prop);
					if (rc) {
						pr_err("Failed to get temp from property\n");
						rc = -1;
					}
					val->intval = prop.intval;
				}
			}
			if (rc) {
				pr_err("Unable to read battery \
						temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
			val->intval = 0;
		}
#endif
		break;

	case LGE_POWER_PROP_USB_ID_PHY:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->usb_id_channel != 0xFF) {
				pr_debug("USB ID channel : %d!!\n",
					lge_adc_chip->usb_id_channel);
#ifdef CONFIG_MACH_MSM8996_LUCYE
				mutex_lock(&lge_adc_chip->lock_for_usb_id);

				usb_pd_psy = power_supply_get_by_name("usb_pd");
				if(usb_pd_psy && usb_pd_psy->get_property) {
					usb_pd_psy->get_property(usb_pd_psy, POWER_SUPPLY_PROP_TYPEC_MODE, &prop);
					typec_accessory = prop.intval;
					pr_info("usb_pd : %d\n", typec_accessory);
				}else{
					pr_info("usb_pd is not ready\n");
				}

				if(usb_pd_psy && usb_pd_psy->get_property) {
					usb_pd_psy->get_property(usb_pd_psy, POWER_SUPPLY_PROP_INPUT_SUSPEND, &prop);
					waterproof = prop.intval;
					pr_info("waterproof : %d\n", waterproof);
				}else{
					pr_info("battery_psy is not ready\n");
				}

				if ((!waterproof && POWER_SUPPLY_TYPE_CTYPE_DEBUG_ACCESSORY == typec_accessory) ||
				    lge_get_board_rev_no() >= HW_REV_1_3) {
					/* SBU_EN low, SBU_SEL high */
					data = 0x11;
					set_pm_gpio_value(lge_adc_chip->pm_vadc, 0xc240, &data, 1);
					data = 0x03;
					set_pm_gpio_value(lge_adc_chip->pm_vadc, 0xc245, &data, 1);
					usleep_range(400, 410);

					gpiod_direction_output(gpio_to_desc(lge_adc_chip->sbu_en), 0);
					usleep_range(400, 410);

					/* wait 100ms for usb_id settling*/
					usleep_range(100000, 101000);

					rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
						lge_adc_chip->usb_id_channel, &results);

					/* SBU_EN high */
					if (typec_accessory != POWER_SUPPLY_TYPE_CTYPE_DEBUG_ACCESSORY) {
						pr_info("[BSP-USB] SBU EN high: waterproof(%d), physical(%d)\n",
							waterproof, (int)results.physical);
						gpiod_direction_output(gpio_to_desc(lge_adc_chip->sbu_en), 1);
					}

					data = 0x10;
					set_pm_gpio_value(lge_adc_chip->pm_vadc, 0xc240, &data, 1);
					data = 0x01;
					set_pm_gpio_value(lge_adc_chip->pm_vadc, 0xc245, &data, 1);
				} else {
					/* SBU_EN high */
					gpiod_direction_output(gpio_to_desc(lge_adc_chip->sbu_en), 1);

					rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
						lge_adc_chip->usb_id_channel, &results);
				}
				mutex_unlock(&lge_adc_chip->lock_for_usb_id);
#else
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->usb_id_channel, &results);
#endif
			} else {
				pr_err("VADC is not used for \
						USB_ID!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read usb id adc! rc : %d!!\n", rc);
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			pr_err("VADC is not ready\n");
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_USB_ID_RAW:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->usb_id_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->usb_id_channel, &results);
			} else {
				pr_err("VADC is not used for USB_ID!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read usb id adc!!! rc : %d!!\n",
						rc);
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_PA0_THERM_PHY:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->pa0_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->pa0_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_PA0_THERM_RAW:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->pa0_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->pa0_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
				val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_PA1_THERM_PHY:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->pa1_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->pa1_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_PA1_THERM_RAW:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->pa1_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->pa1_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
				val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_BD1_THERM_PHY:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->bd1_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->bd1_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_BD1_THERM_RAW:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->bd1_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->bd1_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
				val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_BD2_THERM_PHY:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->bd2_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->bd2_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.physical;
			}
		} else {
			val->intval = 0;
		}
		break;

	case LGE_POWER_PROP_BD2_THERM_RAW:
		if (!IS_ERR(lge_adc_chip->pm_vadc)) {
			if (lge_adc_chip->bd2_therm_channel != 0xFF) {
				rc = qpnp_vadc_read(lge_adc_chip->pm_vadc,
					lge_adc_chip->bd2_therm_channel, &results);
			} else {
				pr_err("VADC is not used for PA_THERM!!!\n");
				rc = -1;
			}
			if (rc) {
				pr_err("Unable to read pa temperature adc!!!\n");
				ret_val = -EINVAL;
			} else {
				val->int64val = results.adc_code;
			}
		} else {
				val->intval = 0;
		}
		break;

	default:
		ret_val = -EINVAL;
		break;
	}

	return ret_val;
}

static int lge_adc_qct_probe(struct platform_device *pdev)
{
	struct lge_adc *lge_adc_chip;
	struct lge_power *lge_power_adc;
	union power_supply_propval prop = {0, };
	int ret = 0;

	pr_err("lge_battery_probe starts!!!\n");
	lge_adc_chip = kzalloc(sizeof(struct lge_adc),
						GFP_KERNEL);
	if (!lge_adc_chip) {
		dev_err(&pdev->dev, "Failed to alloc driver structure\n");
		return -ENOMEM;
	}

	lge_adc_chip->pm_vadc = qpnp_get_vadc(&pdev->dev, "pm_adc");
	if (IS_ERR(lge_adc_chip->pm_vadc)) {
		ret = PTR_ERR(lge_adc_chip->pm_vadc);
		if (ret != -EPROBE_DEFER)
			pr_err("pm-vadc property fail to get\n");
		else {
			pr_err("Not yet initializing pm-vadc\n");
			goto err_free;
		}
	}

	lge_adc_chip->pmi_vadc = qpnp_get_vadc(&pdev->dev, "pmi_adc");
	if (IS_ERR(lge_adc_chip->pmi_vadc)) {
		ret = PTR_ERR(lge_adc_chip->pmi_vadc);
		if (ret != -EPROBE_DEFER)
			pr_err("pmi-vadc property fail to get\n");
		else {
			pr_err("not yet initializing pmi-vadc\n");
			goto err_free;
		}
	}
	lge_adc_chip->bms_psy = power_supply_get_by_name("bms");
	if (lge_adc_chip->bms_psy == NULL) {
		pr_err("Failed to get bms property\n");
		ret = -EPROBE_DEFER;
		goto err_free;
	}

	lge_adc_chip->lge_cc_lpc = lge_power_get_by_name("lge_cc");
	if (!lge_adc_chip->lge_cc_lpc)
		pr_err("No yet charging_cotroller\n");

#ifdef CONFIG_MACH_MSM8996_LUCYE
	mutex_init(&lge_adc_chip->lock_for_usb_id);

	lge_adc_chip->sbu_en = of_get_named_gpio(pdev->dev.of_node, "lge,gpio-sbu-en", 0);

	if(!gpio_is_valid(lge_adc_chip->sbu_en)) {
		pr_err("can't find gpio-sbu-en\n");
		ret = -EPROBE_DEFER;
		goto err_free;
	} else {
		pr_info("find gpio-sbu-en %d!!!\n", lge_adc_chip->sbu_en);
		ret = gpio_request_one(lge_adc_chip->sbu_en, GPIOF_OUT_INIT_HIGH, "gpio-sbu-en");
		if(ret){
			pr_err("fail to request GPIO !!! %d\n", ret);
			ret = -EPROBE_DEFER;
			goto err_free;
		}
	}
#endif

	ret = of_property_read_u32(pdev->dev.of_node, "lge,xo_therm_chan",
				&lge_adc_chip->xo_therm_channel);

	ret = of_property_read_u32(pdev->dev.of_node, "lge,pa0_therm_chan",
				&lge_adc_chip->pa0_therm_channel);

	ret = of_property_read_u32(pdev->dev.of_node, "lge,pa1_therm_chan",
				&lge_adc_chip->pa1_therm_channel);

	ret = of_property_read_u32(pdev->dev.of_node, "lge,bd1_therm_chan",
				&lge_adc_chip->bd1_therm_channel);

	ret = of_property_read_u32(pdev->dev.of_node, "lge,bd2_therm_chan",
				&lge_adc_chip->bd2_therm_channel);

#ifdef CONFIG_ARCH_MSM8996
	ret = lge_adc_chip->bms_psy->get_property(lge_adc_chip->bms_psy,
			POWER_SUPPLY_PROP_TEMP, &prop);
	lge_adc_chip->batt_therm_channel = prop.intval;
#else
	ret = of_property_read_u32(pdev->dev.of_node, "lge,batt_therm_chan",
				&lge_adc_chip->batt_therm_channel);
#endif
	ret = of_property_read_u32(pdev->dev.of_node, "lge,usb_id_chan",
				&lge_adc_chip->usb_id_channel);

	pr_err("XO_THERM_CH : %d, PA0_THERM_CH : %d, PA1_THERM_CH : %d\n",
				lge_adc_chip->xo_therm_channel,
				lge_adc_chip->pa0_therm_channel,
				lge_adc_chip->pa1_therm_channel);

	pr_err("BD1_THERM_CH : %d, BD2_THERM_CH : %d\n",
				lge_adc_chip->bd1_therm_channel,
				lge_adc_chip->bd2_therm_channel);

	pr_err("BATT_THERM_CH : %d, USB_ID_CH : %d\n",
				lge_adc_chip->batt_therm_channel,
				lge_adc_chip->usb_id_channel);

	lge_power_adc = &lge_adc_chip->lge_adc_lpc;

	lge_power_adc->name = "lge_adc";
	lge_power_adc->properties = lge_power_adc_properties;
	lge_power_adc->num_properties = ARRAY_SIZE(lge_power_adc_properties);
	lge_power_adc->get_property = lge_power_adc_get_property;

	ret = lge_power_register(&pdev->dev, lge_power_adc);
	if (ret < 0) {
		pr_err("Failed to register lge power class: %d\n",
		ret);
		goto err_free;
	}
	lge_adc_chip->dev = &pdev->dev;

	pr_info("lge_battery_probe done!!!\n");

	return 0;

err_free:
#ifdef CONFIG_MACH_MSM8996_LUCYE
	gpio_free(lge_adc_chip->sbu_en);
#endif
	kfree(lge_adc_chip);
	return ret;
}

static int lge_adc_qct_remove(struct platform_device *pdev)
{
	struct lge_adc *lge_adc_chip = platform_get_drvdata(pdev);


	lge_power_unregister(&lge_adc_chip->lge_adc_lpc);

	platform_set_drvdata(pdev, NULL);
	kfree(lge_adc_chip);
	return 0;
}
#ifdef CONFIG_OF
static struct of_device_id lge_adc_qct_match_table[] = {
	{.compatible = "lge,adc-qct"},
	{ },
};
#endif
static struct platform_driver lge_adc_qct_driver = {
	.probe = lge_adc_qct_probe,
	.remove = lge_adc_qct_remove,
	.driver = {
	.name = "lge_adc_qct",
	.owner = THIS_MODULE,
#ifdef CONFIG_OF
	.of_match_table = lge_adc_qct_match_table,
#endif
	},
};

static int __init lge_adc_qct_init(void)
{
	return platform_driver_register(&lge_adc_qct_driver);
}


static void __init lge_adc_qct_exit(void)
{
	platform_driver_unregister(&lge_adc_qct_driver);
}

module_init(lge_adc_qct_init);
module_exit(lge_adc_qct_exit);

MODULE_AUTHOR("Daeho Choi <daeho.choi@lge.com>");
MODULE_DESCRIPTION("Driver for LGE ADC driver for QCT");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:LGE ADC for QCT");
