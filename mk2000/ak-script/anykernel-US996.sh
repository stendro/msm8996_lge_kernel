# AnyKernel2 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=US996 mk2000
do.devicecheck=1
do.droidcheck=1
do.modules=1
do.cleanup=1
do.cleanuponabort=0
device.name1=us996
device.name2=elsa
device.name3=US996
device.name4=ELSA
'; } # end properties

# shell variables
block=/dev/block/bootdevice/by-name/boot;
is_slot_device=0;
ramdisk_compression=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. /tmp/anykernel/tools/ak2-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
chmod -R 750 $ramdisk/*;
chmod -R 755 $ramdisk/sbin;
chown -R root:root $ramdisk/*;

## AnyKernel install
dump_boot;

## Ramdisk modifications
# make sure adb is working, enable blu_active tweaks, disable forced encryption and triton
patch_prop default.prop "ro.secure" "1";
patch_prop default.prop "ro.adb.secure" "1";
append_file init.rc blu_active "init_rc-mod";
patch_fstab fstab.elsa /data ext4 flags "forceencrypt=" "encryptable=";
replace_section init.elsa.power.rc "service triton" " " "service triton /system/vendor/bin/triton\n   class main\n   user root\n   group system\n   socket triton-client stream 660 system system\n   disabled\n   oneshot\n";

## System modifications
# make sure init.blu_active.rc can run, and disable rctd
mount -o rw,remount -t auto /system;
append_file /system/vendor/bin/init.qcom.post_boot.sh sys.post_boot.parsed "post_boot-mod";
remove_section /system/vendor/etc/init/init.lge.rc "service rctd" " ";
mount -o ro,remount -t auto /system;

write_boot;
## end install
