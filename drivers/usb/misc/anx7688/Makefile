obj-$(CONFIG_LGE_USB_ANX7688) := anx_i2c_intf.o anx7688_core.o anx7688_firmware.o anx7688_pd.o
obj-$(CONFIG_LGE_DP_ANX7688) += anx7688_dp.o

ifeq ($(CONFIG_LGE_USB_ANX7688),y)
obj-$(CONFIG_DEBUG_FS) += anx7688_debugfs.o
endif
