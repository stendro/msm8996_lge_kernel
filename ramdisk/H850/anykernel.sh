# AnyKernel2 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() {
kernel.string=H850 mk2000
do.devicecheck=1
do.postboot=1
do.modules=1
do.cleanup=1
do.cleanuponabort=0
device.name1=h850
device.name2=h1
device.name3=H850
device.name4=H1
} # end properties

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
chown -R root:root $ramdisk/*;

## MK2000 begin
ui_print " ";
ui_print "  ___   ___  __  ___     ____   ___   ___   ___   ";
ui_print " |   \_/   ||  |/  /    / _  \ / _ \ / _ \ / _ \  ";
ui_print " |         ||     ‹    |_/ / // / \ | / \ | / \ \ ";
ui_print " |  |\_/|  ||  |\  \    __/ /_\ \_/ | \_/ | \_/ / ";
ui_print " |__|   |__||__| \__\  |______|\___/ \___/ \___/  ";
ui_print " ";

## AnyKernel install
dump_boot;

## Ramdisk modifications
# prop - make sure adb is working, enable blu_active tweaks, disable rctd & triton
patch_prop default.prop "ro.secure" "1";
patch_prop default.prop "ro.adb.secure" "1";
remove_section init.lge.rc "service rctd" " ";
insert_line init.rc blu_active after "import /init.lge.rc" "import /init.blu_active.rc";
replace_section init.h1.power.rc "service triton" " " "service triton /system/bin/triton\n   class main\n   user root\n   group system\n   socket triton-client stream 660 system system\n   disabled\n   oneshot\n";

write_boot;
## end install
