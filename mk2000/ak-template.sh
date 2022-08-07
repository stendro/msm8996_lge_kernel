#!/bin/bash
# Generate AnyKernel3 Script

if [ "$DEVICE" = "H870" ] || [ "$DEVICE" = "US997" ] || [ "$DEVICE" = "H872" ]; then
  DEV_NAME=LUCYE
elif [ "$DEVICE" = "H850" ] || [ "$DEVICE" = "RS988" ] || [ "$DEVICE" = "H830" ]; then
  DEV_NAME=H1
elif [ "$DEVICE" = "US996Dirty" ] || [ "$DEVICE" = "US996" ] || [ "$DEVICE" = "H918" ]; then
  DEV_NAME=ELSA
else
  DEV_NAME=ELSA
  DEV_ABOOT=US996
  ABOOT_LOW=us996
fi

if [ "$DEVICE" = "US996Dirty" ]; then
  AK_DEV=US996
else
  AK_DEV=$DEVICE
fi

DEV_LOW=$(echo "$AK_DEV" | awk '{print tolower($0)}')
NAME_LOW=$(echo "$DEV_NAME" | awk '{print tolower($0)}')

cat << EOF
# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=$DEVICE Kernel by askermk2000 @ xda-developers
do.devicecheck=1
do.modules=0
do.ssdtrim=0
do.cleanup=1
do.cleanuponabort=0
device.name1=$AK_DEV
device.name2=$DEV_NAME
device.name3=$DEV_LOW
device.name4=$NAME_LOW
device.name5=$DEV_ABOOT
device.name6=$ABOOT_LOW
'; } # end properties

# shell variables
block=/dev/block/bootdevice/by-name/boot;
is_slot_device=0;
ramdisk_compression=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
chmod -R 750 \$ramdisk/*;
chown -R root:root \$ramdisk/*;

## AnyKernel install
dump_boot;

## Ramdisk modifications
# add mktweaks
# append_file init.rc mktweaks "init_rc-mod";

write_boot;
## end install
EOF
