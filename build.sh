#!/bin/bash
# Stock kernel for LG Electronics msm8996 devices build script by jcadduono

################### BEFORE STARTING ################
#
# download a working toolchain and extract it somewhere and configure this
# file to point to the toolchain's root directory.
#
# once you've set up the config section how you like it, you can simply run
# ./build.sh [VARIANT]
#
##################### VARIANTS #####################
#
# h870   = International (Global)
#          LGH870   (LG G6)
#
# us997  = US Cellular & Unlocked (US)
#          US997    (LG G6)
#
###################### CONFIG ######################

# root directory of LGE msm8996 git repo (default is this script's location)
RDIR=$(pwd)

[ "$VER" ] ||
# version number
VER=$(cat "$RDIR/VERSION")

# directory containing cross-compile arm64 toolchain
TOOLCHAIN=$HOME/build/toolchain/bin/aarch64-linux-gnu-
# TOOLCHAIN=$HOME/build/toolchain/gcc7/bin/aarch64-linux-gnu-

CPU_THREADS=$(grep -c "processor" /proc/cpuinfo)
# amount of cpu threads to use in kernel make process
THREADS=2

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
[ "$DEVICE" ] || DEVICE=h870

DEFCONFIG=${TARGET}_defconfig
DEVICE_DEFCONFIG=device_lge_${DEVICE}

[ -f "$RDIR/arch/$ARCH/configs/${DEFCONFIG}" ] ||
ABORT "Config $DEFCONFIG not found in $ARCH configs!"

[ -f "$RDIR/arch/$ARCH/configs/${DEVICE_DEFCONFIG}" ] ||
ABORT "Device config $DEVICE_DEFCONFIG not found in $ARCH configs!"

KDIR="$RDIR/build/arch/$ARCH/boot"
export LOCALVERSION=$TARGET-$DEVICE-$VER

CLEAN_BUILD() {
	echo "Cleaning build..."
	rm -rf build
}

SETUP_BUILD() {
	echo "Creating kernel config for $LOCALVERSION..."
	mkdir -p build
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
	rm build/lib/modules/*/build build/lib/modules/*/source
}

cd "$RDIR" || ABORT "Failed to enter $RDIR!"

CLEAN_BUILD &&
SETUP_BUILD &&
BUILD_KERNEL &&
INSTALL_MODULES &&
echo "Finished building $LOCALVERSION!"
