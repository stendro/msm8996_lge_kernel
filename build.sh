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
# ./build.sh [VARIANT]
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
#		LGUS996  (LG V20) (Unlocked with Kernel Exploit)
#
# VS995		= Verizon (US)
#		LGVS995  (LG V20)
#
# H990DS/TR	= International (Global)
#		LGH990   (LG V20)
#
# LS997		= Sprint (US)
#		LGLS997  (LG V20)
#
# F800K		= Korea (KR)
#		LGF800K  (LG V20)
#
#   *************************
#
# H870		= International (Global)
#		LGH870   (LG G6)
#
# US997		= US Cellular & Unlocked (US)
#		US997    (LG G6)
#
# H872		= T-Mobile (US)
#		LGH872   (LG G6)
#
###################### CONFIG ######################

# root directory of LGE msm8996 git repo (default is this script's location)
RDIR=$(pwd)

[ "$VER" ] ||
# version number
VER=$(cat "$RDIR/VERSION")

# directory containing cross-compile arm64 toolchain
TOOLCHAIN=$HOME/build/toolchain/bin/aarch64-linux-gnu-

CPU_THREADS=$(grep -c "processor" /proc/cpuinfo)
# amount of cpu threads to use in kernel make process
# I'm using a VM on a slow pc...
THREADS=2
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

[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || ABORT "No device specified"

# link device name to lg config files
if [ "$DEVICE" = "H850" ]; then
  DEVICE_DEFCONFIG=h1_global_com-perf_defconfig
fi
if [ "$DEVICE" = "H830" ]; then
  DEVICE_DEFCONFIG=h1_tmo_us-perf_defconfig
fi
if [ "$DEVICE" = "RS988" ]; then
  DEVICE_DEFCONFIG=h1_lra_us-perf_defconfig
fi
if [ "$DEVICE" = "H870" ]; then
  DEVICE_DEFCONFIG=lucye_global_com-perf_defconfig
fi
if [ "$DEVICE" = "US997" ]; then
  DEVICE_DEFCONFIG=lucye_nao_us-perf_defconfig
fi
if [ "$DEVICE" = "H872" ]; then
  DEVICE_DEFCONFIG=lucye_tmo_us-perf_defconfig
fi
if [ "$DEVICE" = "H990DS" ]; then
  DEVICE_DEFCONFIG=elsa_global_com-perf_defconfig
fi
if [ "$DEVICE" = "H990TR" ]; then
  DEVICE_DEFCONFIG=elsa_cno_cn-perf_defconfig
fi
if [ "$DEVICE" = "US996" ]; then
  DEVICE_DEFCONFIG=elsa_nao_us-perf_defconfig
fi
if [ "$DEVICE" = "US996Santa" ]; then
  DEVICE_DEFCONFIG=elsa_usc_us-perf_defconfig
fi
if [ "$DEVICE" = "LS997" ]; then
  DEVICE_DEFCONFIG=elsa_spr_us-perf_defconfig
fi
if [ "$DEVICE" = "VS995" ]; then
  DEVICE_DEFCONFIG=elsa_vzw-perf_defconfig
fi
if [ "$DEVICE" = "H918" ]; then
  DEVICE_DEFCONFIG=elsa_tmo_us-perf_defconfig
fi
if [ "$DEVICE" = "H910" ]; then
  DEVICE_DEFCONFIG=elsa_att_us-perf_defconfig
fi
if [ "$DEVICE" = "F800K" ]; then
  DEVICE_DEFCONFIG=elsa_kt_kr-perf_defconfig
fi

[ -f "$RDIR/arch/$ARCH/configs/${DEVICE_DEFCONFIG}" ] ||
ABORT "Device config $DEVICE_DEFCONFIG not found in $ARCH configs!"

[ -x "${CROSS_COMPILE}gcc" ] ||
ABORT "Unable to find gcc cross-compiler at location: ${CROSS_COMPILE}gcc"

KDIR="$RDIR/build/arch/$ARCH/boot"
export LOCALVERSION=${DEVICE}_${VER}-mk2000

CLEAN_BUILD() {
	echo "Cleaning build..."
	rm -rf build
}

SETUP_BUILD() {
	echo "Creating kernel config..."
	mkdir -p build
	echo "$DEVICE" > build/DEVICE \
		|| ABORT "Failed to reflect device"
	make -C "$RDIR" O=build "$DEVICE_DEFCONFIG" \
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
echo "Starting build for ${DEVICE} ${VER}"

CLEAN_BUILD &&
SETUP_BUILD &&
BUILD_KERNEL &&
INSTALL_MODULES &&
echo "Finished building $LOCALVERSION - Run ./copy_finished.sh"
