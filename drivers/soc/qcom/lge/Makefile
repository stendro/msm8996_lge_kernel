subdir-ccflags-y := -DDEBUG

obj-$(CONFIG_MACH_LGE) += devices_lge.o
obj-$(CONFIG_LGE_BOOTLOADER_LOG) += lge_bootloader_log.o
#obj-$(CONFIG_LGE_PM_CHARGING_CONTROLLER) += lge_charging_controller.o
ifdef CONFIG_LGE_PM_CHARGING_SCENARIO_V18
obj-$(CONFIG_LGE_PM_CHARGING_SCENARIO) += lge_charging_scenario_v18.o
else
obj-$(CONFIG_LGE_PM_CHARGING_SCENARIO) += lge_charging_scenario.o
endif
#obj-$(CONFIG_LGE_PM_UNIFIED_NODE) += lge_power_sysfs.o
obj-$(CONFIG_LGE_PM_PSEUDO_BATTERY) += lge_pseudo_batt.o
obj-$(CONFIG_LGE_LCD_TUNING) += lge_lcd_misc_ctrl.o
obj-$(CONFIG_LGE_PM_SUPPORT_LG_POWER_CLASS) += power/
