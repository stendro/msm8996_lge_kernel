#!/bin/bash
# simple script for executing menuconfig
# -modified by stendro (source: jcadduono)
#
# It currently puts the output in "../"

# root directory of this kernel (this script's location)
RDIR=$(pwd)

# build dir
BDIR=build

# color codes
COLOR_N="\033[0m"
COLOR_R="\033[0;31m"
COLOR_G="\033[1;32m"

# selected device
[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || ABORT "No device specified!"

# config output dir/file
OUTDIR=$(dirname "$RDIR")
OUTFILE=${DEVICE}_config_regen

# directory containing cross-compiler
TOOLCHAIN=$HOME/mk2000/toolchain/stendro/aarch64-elf/bin/aarch64-elf-

export ARCH=arm64
export CROSS_COMPILE=$TOOLCHAIN

ABORT() {
	echo -e $COLOR_R"Error: $*"
	exit 1
}

# link device name to lg config files
if [ "$DEVICE" = "H850" ]; then
  DEVICE_DEFCONFIG=lineageos_h850_defconfig
elif [ "$DEVICE" = "H830" ]; then
  DEVICE_DEFCONFIG=lineageos_h830_defconfig
elif [ "$DEVICE" = "RS988" ]; then
  DEVICE_DEFCONFIG=lineageos_rs988_defconfig
elif [ "$DEVICE" = "H870" ]; then
  DEVICE_DEFCONFIG=lineageos_h870_defconfig
elif [ "$DEVICE" = "H872" ]; then
  DEVICE_DEFCONFIG=lineageos_h872_defconfig
elif [ "$DEVICE" = "US997" ]; then
  DEVICE_DEFCONFIG=lineageos_us997_defconfig
elif [ "$DEVICE" = "H918" ]; then
  DEVICE_DEFCONFIG=lineageos_h918_defconfig
elif [ "$DEVICE" = "H910" ]; then
  DEVICE_DEFCONFIG=lineageos_h910_defconfig
elif [ "$DEVICE" = "H990" ]; then
  DEVICE_DEFCONFIG=lineageos_h990_defconfig
elif [ "$DEVICE" = "US996" ]; then
  DEVICE_DEFCONFIG=lineageos_us996_defconfig
elif [ "$DEVICE" = "US996Dirty" ]; then
  DEVICE_DEFCONFIG=lineageos_us996-dirty_defconfig
elif [ "$DEVICE" = "VS995" ]; then
  DEVICE_DEFCONFIG=lineageos_vs995_defconfig
elif [ "$DEVICE" = "LS997" ]; then
  DEVICE_DEFCONFIG=lineageos_ls997_defconfig
else
  ABORT "Invalid device specified! Make sure to use upper-case."
fi

# check for stuff
[ -f "$RDIR/arch/$ARCH/configs/${DEVICE_DEFCONFIG}" ] \
	|| ABORT "$DEVICE_DEFCONFIG not found in $ARCH configs!"

[ -x "${CROSS_COMPILE}gcc" ] \
	|| ABORT "Cross-compiler not found at: ${CROSS_COMPILE}gcc"

cd "$RDIR" || ABORT "Failed to enter $RDIR!"

# start menuconfig
echo -e $COLOR_G"Cleaning build..."$COLOR_N
rm -rf $BDIR
mkdir $BDIR
make -s -i -C "$RDIR" O=$BDIR "$DEVICE_DEFCONFIG" menuconfig
echo -e $COLOR_G"Showing differences between old config and new config"
echo -e $COLOR_R"-----------------------------------------------------"$COLOR_N
make -s -i -C "$RDIR" O=$BDIR "$DEVICE_DEFCONFIG"
if command -v colordiff >/dev/null 2>&1; then
	diff -Bwu --label "old config" $BDIR/.config --label "new config" $BDIR/.config.old | colordiff
else
	diff -Bwu --label "old config" $BDIR/.config --label "new config" $BDIR/.config.old
	echo -e $COLOR_R"-----------------------------------------------------"
	echo -e $COLOR_G"Consider installing the colordiff package to make diffs easier to read"
fi
echo -e $COLOR_R"-----------------------------------------------------"
echo -ne $COLOR_G"Are you satisfied with these changes? Y/N: "
read option
case $option in
y|Y)
	cp $BDIR/.config.old "../$OUTFILE"
	echo "Copied new config to $OUTDIR/$OUTFILE"
	;;
*)
	echo "That's unfortunate"
	;;
esac
echo "Done."
