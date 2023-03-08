/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
/*
 * Copyright (C) 2014 Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */

/*
 * qpnp-smbcharger.c macros
 */
#define IDEV_STS	0x8

#define USB_DP		67
#define USB_DM		68


/*
 * qpnp-smbcharger_extension_usb.c macros
 */
#define DEFAULT_PROP1000_MA		1000
#define DEFAULT_PROP500_MA		500
#define THR_DP_DM_FLOAT_UV		3030000
#define THR_DP_GND_UV			100000

#define ID_POLLING_TIMEOUT_DEFAULT		(30000)
#define ID_POLLING_TIMEOUT_MAX			(60000)
#define ID_POLLING_WAKE_LOCK_TIMEOUT_EXT	(1000)

static struct usb_somc_params *musb_params;

static bool force_id_polling_on;
module_param(force_id_polling_on, bool, S_IRUSR | S_IWUSR);

static bool id_polling_state;
module_param(id_polling_state, bool, S_IRUSR);

static unsigned int id_polling_timeout;
module_param(id_polling_timeout, uint, S_IRUSR | S_IWUSR);

static bool start_id_polling;

/*
 * qpnp-smbcharger.c functions
 */
static int smbchg_read(struct smbchg_chip *chip, u8 *val, u16 addr, int count);
static int get_type(u8 type_reg);
static void update_typec_otg_status(struct smbchg_chip *chip, int mode,
								bool force);


/*
 * qpnp-smbcharger_extension_usb.c functions
 */
static void somc_chg_usbid_start_polling(struct usb_somc_params *params);
static void somc_chg_usbid_stop_polling(struct usb_somc_params *params);
static int set_start_id_polling(const char *val, const struct kernel_param *kp)
{
	struct somc_typec_mode_ctrl *typecctrl;
	struct usb_somc_params *usb_params;
	struct smbchg_chip *chip;
	int ret;

	if (musb_params == NULL) {
		pr_err("not yet initialized\n");
		return -ENODEV;
	}

	ret = param_set_bool(val, kp);

	usb_params = musb_params;
	chip = container_of(usb_params, struct smbchg_chip, usb_params);
	typecctrl = &usb_params->typecctrl;

	if (start_id_polling) {
		if (id_polling_timeout > ID_POLLING_TIMEOUT_MAX)
			id_polling_timeout = ID_POLLING_TIMEOUT_MAX;

		dev_dbg(chip->dev, "user request polling start\n");
		cancel_delayed_work_sync(&typecctrl->stop_polling_delay);
		queue_delayed_work(typecctrl->polling_wq,
				&typecctrl->stop_polling_delay,
				msecs_to_jiffies(id_polling_timeout));

		__pm_wakeup_event(&typecctrl->wakeup_source_id_polling,
					(id_polling_timeout +
					ID_POLLING_WAKE_LOCK_TIMEOUT_EXT));
		typecctrl->user_request_polling = true;
		somc_chg_usbid_start_polling(usb_params);
	} else {
		dev_dbg(chip->dev, "user request polling stop\n");
		typecctrl->user_request_polling = false;
		somc_chg_usbid_stop_polling(usb_params);
	}

	return ret;
}

static struct kernel_param_ops start_id_polling_ops = {
	.set = set_start_id_polling,
	.get = param_get_bool,
};
module_param_cb(start_id_polling, &start_id_polling_ops, &start_id_polling,
							S_IRUSR | S_IWUSR);


static void somc_chg_usbid_start_polling_delay_work(struct work_struct *work)
{
	struct somc_typec_mode_ctrl *typecctrl = container_of(work,
						struct somc_typec_mode_ctrl,
						start_polling_delay.work);
	struct usb_somc_params *usb_params = container_of(typecctrl,
						struct usb_somc_params,
						typecctrl);
	struct smbchg_chip *chip = container_of(usb_params,
						struct smbchg_chip,
						usb_params);

	dev_dbg(chip->dev, "start id polling\n");

	if ((chip->typec_received_mode == POWER_SUPPLY_TYPE_DFP) &&
							!chip->typec_dfp) {
		dev_info(chip->dev, "detect pseudo disconnected\n");
		update_typec_otg_status(chip, POWER_SUPPLY_TYPE_DFP, false);
	}

	if (chip->typec_psy) {
		union power_supply_propval val = {0, };

		val.intval = POWER_SUPPLY_TYPE_TYPEC;
		power_supply_set_property(chip->typec_psy,
				POWER_SUPPLY_PROP_TYPEC_MODE, &val);
	}
}

static void somc_chg_usbid_start_polling(struct usb_somc_params *usb_params)
{
	struct somc_typec_mode_ctrl *typecctrl = &usb_params->typecctrl;
	struct smbchg_chip *chip = container_of(usb_params,
						struct smbchg_chip,
						usb_params);

	id_polling_state = true;

	dev_dbg(chip->dev, "queue id polling\n");
	queue_delayed_work(typecctrl->polling_wq,
					&typecctrl->start_polling_delay, 0);
}

static void somc_chg_usbid_stop_polling_delay_work(struct work_struct *work)
{
	struct somc_typec_mode_ctrl *typecctrl = container_of(work,
						struct somc_typec_mode_ctrl,
						stop_polling_delay.work);
	struct usb_somc_params *usb_params = container_of(typecctrl,
						struct usb_somc_params,
						typecctrl);
	struct smbchg_chip *chip = container_of(usb_params,
						struct smbchg_chip,
						usb_params);

	if (force_id_polling_on) {
		dev_dbg(chip->dev,
			"force ID Polling. do not stop polling\n");
		goto out;
	}

	dev_dbg(chip->dev, "stop id polling\n");

	if (chip->typec_psy) {
		union power_supply_propval val = {0, };

		val.intval = POWER_SUPPLY_TYPE_UFP;
		power_supply_set_property(chip->typec_psy,
				POWER_SUPPLY_PROP_TYPEC_MODE, &val);
	}
	start_id_polling = false;

out:
	typecctrl->user_request_polling = false;
	if (typecctrl->wakeup_source_id_polling.active)
		__pm_relax(&typecctrl->wakeup_source_id_polling);
}

static void somc_chg_usbid_stop_polling(struct usb_somc_params *usb_params)
{
	struct somc_typec_mode_ctrl *typecctrl = &usb_params->typecctrl;

	if (typecctrl->user_request_polling)
		return;

	cancel_delayed_work_sync(&typecctrl->stop_polling_delay);
	queue_delayed_work(typecctrl->polling_wq,
					&typecctrl->stop_polling_delay, 0);
}

/*
 * USB OCP part
 */
static const char *rdev_get_name(struct regulator_dev *rdev)
{
	if (rdev->constraints && rdev->constraints->name)
		return rdev->constraints->name;
	else if (rdev->desc->name)
		return rdev->desc->name;
	else
		return "";
}

static int somc_usb_otg_regulator_register_ocp_notification(
				struct regulator_dev *rdev,
				struct regulator_ocp_notification *notification)
{
	struct smbchg_chip *chip = rdev_get_drvdata(rdev);
	struct somc_usb_ocp *ocp = &chip->usb_params.ocp;
	unsigned long flags;

	spin_lock_irqsave(&ocp->lock, flags);
	if (notification)
		/* register ocp notification */
		ocp->notification = *notification;
	else
		/* unregister ocp notification */
		memset(&ocp->notification, 0, sizeof(ocp->notification));
	spin_unlock_irqrestore(&ocp->lock, flags);

	pr_smb(PR_MISC, "%s: registered ocp notification(notify=%p, ctxt=%p)\n",
						rdev_get_name(rdev),
						ocp->notification.notify,
						ocp->notification.ctxt);

	return 0;
}

static int somc_usb_otg_regulator_ocp_notify(struct smbchg_chip *chip)
{
	struct somc_usb_ocp *ocp = &chip->usb_params.ocp;
	unsigned long flags;

	spin_lock_irqsave(&ocp->lock, flags);
	if (ocp->notification.notify)
		ocp->notification.notify(ocp->notification.ctxt);
	spin_unlock_irqrestore(&ocp->lock, flags);

	return 0;
}

static int somc_usb_register(struct smbchg_chip *chip)
{
	struct usb_somc_params *params = &chip->usb_params;
	struct somc_usb_ocp *ocp = &params->ocp;
	struct somc_typec_mode_ctrl *typecctrl = &params->typecctrl;

	memset(&ocp->notification, 0, sizeof(ocp->notification));
	spin_lock_init(&ocp->lock);

	musb_params = params;

	typecctrl->polling_wq = create_singlethread_workqueue("id-polling_wq");
	INIT_DELAYED_WORK(&typecctrl->start_polling_delay,
				somc_chg_usbid_start_polling_delay_work);
	INIT_DELAYED_WORK(&typecctrl->stop_polling_delay,
				somc_chg_usbid_stop_polling_delay_work);

	wakeup_source_init(&typecctrl->wakeup_source_id_polling,
					"wakeup_source_id_polling");

	typecctrl->user_request_polling = false;
	typecctrl->avoid_first_usbid_change = false;

	id_polling_state = false;
	start_id_polling = false;
	id_polling_timeout = ID_POLLING_TIMEOUT_DEFAULT;
	force_id_polling_on = false;

	pr_smb(PR_MISC, "somc usb register success\n");
	return 0;
}

static void somc_usb_unregister(struct smbchg_chip *chip)
{
	struct somc_typec_mode_ctrl *typecctrl = &chip->usb_params.typecctrl;

	cancel_delayed_work_sync(&typecctrl->stop_polling_delay);
	cancel_delayed_work_sync(&typecctrl->start_polling_delay);
	destroy_workqueue(typecctrl->polling_wq);

	wakeup_source_trash(&typecctrl->wakeup_source_id_polling);
}
