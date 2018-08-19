# AnyKernel2 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=H910 mk2000
do.devicecheck=1
do.postboot=1
do.modules=1
do.cleanup=1
do.cleanuponabort=0
device.name1=h910
device.name2=elsa
device.name3=H910
device.name4=ELSA
device.name5=us996
device.name6=US996
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
# prop - make sure adb is working, enable blu_active tweaks, disable rctd & triton
patch_prop default.prop "ro.secure" "1";
patch_prop default.prop "ro.adb.secure" "1";
append_file init.rc blu_active "init_rc-mod";
remove_section init.lge.rc "service rctd" " ";
replace_section init.elsa.power.rc "service triton" " " "service triton /system/bin/triton\n   class main\n   user root\n   group system\n   socket triton-client stream 660 system system\n   disabled\n   oneshot\n";

write_boot;
## end install
