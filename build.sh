#!/bin/bash
#
# Stock kernel for LG Electronics msm8996 devices build script by jcadduono
# -modified by stendro
#
################### BEFORE STARTING ################
#
# download a working toolchain and extract it somewhere and configure this
# file to point to the toolchain's root directory.
#
# once you've set up the config section how you like it, you can simply run
# ./build.sh [VARIANT] (nethunter)
#
##################### VARIANTS #####################
#
# H850		= International (Global)
#		LGH850   (LG G5)
#
# H830		= T-Mobile (US)
#		LGH830   (LG G5)
#
# RS988		= Unlocked (US)
#		LGRS988  (LG G5)
#
#   ************************
#
# H910		= AT&T (US)
#		LGH910   (LG V20)
#
# H918		= T-Mobile (US)
#		LGH918   (LG V20)
#
# US996		= US Cellular & Unlocked (US)
#		LGUS996  (LG V20)
#
# US996Santa	= US Cellular & Unlocked (US)
#		LGUS996  (LG V20) (Unlocked with Kernel Exploit)
#
# VS995		= Verizon (US)
#		LGVS995  (LG V20)
#
# H990		= International (Global)
#		LGH990   (LG V20)
#
# LS997		= Sprint (US)
#		LGLS997  (LG V20)
#
#   ************************
#
# H870		= International (Global)
#		LGH870   (LG G6)
#
# US997		= US Cellular & Unlocked (US)
#		US997    (LG G6)
#
###################### CONFIG ######################

# root directory of LGE msm8996 git repo (default is this script's location)
RDIR=$(pwd)

[ "$VER" ] ||
# version number
VER=$(cat "$RDIR/VERSION")

# directory containing cross-compile arm64 toolchain
#TOOLCHAIN=$HOME/build/toolchain/bin/aarch64-linux-gnu-
TOOLCHAIN=$HOME/build/toolchain/linaro7/bin/aarch64-linaro-linux-android-

CPU_THREADS=$(grep -c "processor" /proc/cpuinfo)
# amount of cpu threads to use in kernel make process
# I'm using a VM on a slow pc...
THREADS=$CPU_THREADS
#THREADS=$((CPU_THREADS + 1))

############## SCARY NO-TOUCHY STUFF ###############

ABORT() {
	[ "$1" ] && echo "Error: $*"
	exit 1
}

export KBUILD_BUILD_USER=stendro
export KBUILD_BUILD_HOST=xda
export ARCH=arm64
export USE_CCACHE=1
export CROSS_COMPILE=$TOOLCHAIN

[ -x "${CROSS_COMPILE}gcc" ] ||
ABORT "Unable to find gcc cross-compiler at location: ${CROSS_COMPILE}gcc"

[ "$TARGET" ] || TARGET=lge
[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || ABORT "No device specified"

if [ $# -eq 2 ] ; then
TARGET=$2	
fi

DEFCONFIG=${TARGET}_defconfig
DEVICE_DEFCONFIG=device_lge_${DEVICE}

[ -f "$RDIR/arch/$ARCH/configs/${DEFCONFIG}" ] ||
ABORT "Config $DEFCONFIG not found in $ARCH configs!"

[ -f "$RDIR/arch/$ARCH/configs/${DEVICE_DEFCONFIG}" ] ||
ABORT "Device config $DEVICE_DEFCONFIG not found in $ARCH configs!"

KDIR="$RDIR/build/arch/$ARCH/boot"
export LOCALVERSION=${TARGET}_${DEVICE}_${VER}-mk2000

CLEAN_BUILD() {
	echo "Cleaning build..."
	#rm -rf build
	rm -rf build/lib/modules
}

SETUP_BUILD() {
	echo "Creating kernel config..."
	mkdir -p build
	echo "$DEVICE" > build/DEVICE \
		|| ABORT "Failed to reflect device"
	make -C "$RDIR" O=build "$DEFCONFIG" \
		DEVICE_DEFCONFIG="$DEVICE_DEFCONFIG" \
		|| ABORT "Failed to set up build"
}

BUILD_KERNEL() {
	echo "Starting build for $LOCALVERSION..."
	while ! make -C "$RDIR" O=build -j"$THREADS"; do
		read -rp "Build failed. Retry? " do_retry
		case $do_retry in
			Y|y) continue ;;
			*) return 1 ;;
		esac
	done
}

INSTALL_MODULES() {
	grep -q 'CONFIG_MODULES=y' build/.config || return 0
	echo "Installing kernel modules to build/lib/modules..."
	make -C "$RDIR" O=build \
		INSTALL_MOD_PATH="." \
		INSTALL_MOD_STRIP=1 \
		modules_install
	#rm build/lib/modules/*/build build/lib/modules/*/source
}

cd "$RDIR" || ABORT "Failed to enter $RDIR!"
echo "Starting build for ${DEVICE} ${VER} ${TARGET}"

CLEAN_BUILD &&
SETUP_BUILD &&
BUILD_KERNEL &&
INSTALL_MODULES &&
#echo "Finished building $LOCALVERSION - Run ./copy_finished.sh"
if [ "$TARGET" = "nethunter" ]; then
	source copy_finished.sh $TARGET
else
	source copy_finished.sh
fi
