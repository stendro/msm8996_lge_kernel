ccflags-y := -DDEBUG -DCONFIG_MAUSB_DEBUG

obj-$(CONFIG_LGE_USB_G_LAF) += mausb-core.o
mausb-core-y := mausb_common.o mausb_event.o

obj-$(CONFIG_LGE_USB_G_LAF) += mausb-device.o
mausb-device-y := stub_dev.o stub_main.o stub_rx.o stub_tx.o mausb_mgmt.o mausb_out.o mausb_in.o mausb_util.o
