#
# Makefile for the QCOM specific video device drivers
# based on V4L2.
#
obj-$(CONFIG_MSM_VIDC_V4L2) += vidc/
obj-y += broadcast/
obj-$(CONFIG_DVB_MPQ) += dvb/
ifdef CONFIG_MACH_MSM8996_LUCYE
obj-$(CONFIG_MSMB_CAMERA) += camera_v2_lucye/
else
obj-$(CONFIG_MSMB_CAMERA) += camera_v2/
endif
obj-y += sde/
