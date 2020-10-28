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
#   *************************
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
#		LGUS996  (LG V20) (Unlocked with Engineering Bootloader)
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
#   *************************
#
# H870		= International (Global)
#		LGH870   (LG G6)
#
# US997		= US Cellular & Unlocked (US)
#		US997    (LG G6)
#
###################### CONFIG ######################

# root directory of this kernel (this script's location)
RDIR=$(pwd)

# build dir
BDIR=build

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

# color codes
COLOR_N="\033[0m"
COLOR_R="\033[0;31m"
COLOR_G="\033[1;32m"
COLOR_P="\033[1;35m"

ABORT() {
	echo -e $COLOR_R"Error: $*"
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
	echo -e $COLOR_G"Cleaning build..."$COLOR_N
	#rm -rf build
	rm -rf build/lib/modules
}

SETUP_BUILD() {
	echo -e $COLOR_G"Creating kernel config..."$COLOR_N
	mkdir -p build
	echo "$DEVICE" > build/DEVICE \
		|| ABORT "Failed to reflect device"
	make -C "$RDIR" O=build "$DEFCONFIG" \
		DEVICE_DEFCONFIG="$DEVICE_DEFCONFIG" \
		|| ABORT "Failed to set up build"
}

BUILD_KERNEL() {
	echo -e $COLOR_G"Starting build for $LOCALVERSION..."$COLOR_N
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
	echo -e $COLOR_G"Installing kernel modules to build/lib/modules..."$COLOR_N
	make -C "$RDIR" O=build \
		INSTALL_MOD_PATH="." \
		INSTALL_MOD_STRIP=1 \
		modules_install
	#rm build/lib/modules/*/build build/lib/modules/*/source
}

PREPARE_NEXT() {
	echo "$DEVICE" > $BDIR/DEVICE \
		|| echo -e $COLOR_R"Failed to reflect device!"
	if grep -q 'CONFIG_BUILD_ARM64_KERNEL_LZ4=y' $BDIR/.config; then
	  echo lz4 > $BDIR/COMPRESSION \
		|| echo -e $COLOR_R"Failed to reflect compression method!"
	else
	  echo gz > $BDIR/COMPRESSION \
		|| echo -e $COLOR_R"Failed to reflect compression method!"
	fi
	git log --oneline -50 > $BDIR/GITCOMMITS \
		|| echo -e $COLOR_R"Failed to reflect commit log!"
}

cd "$RDIR" || ABORT "Failed to enter $RDIR!"
echo -e $COLOR_G"Starting build for ${DEVICE} ${VER} ${TARGET}"

CLEAN_BUILD &&
SETUP_BUILD &&
BUILD_KERNEL &&
INSTALL_MODULES &&
PREPARE_NEXT &&
#echo "Finished building $LOCALVERSION - Run ./copy_finished.sh"
if [ "$TARGET" = "nethunter" ]; then
	source copy_finished.sh $TARGET
else
	source copy_finished.sh
fi
