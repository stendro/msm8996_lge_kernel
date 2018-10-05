#!/bin/bash
# simple script for executing menuconfig
# -modified by stendro (source: jcadduono)

# root directory of this kernel (this script's location)
RDIR=$(pwd)
OUTDIR=$(dirname "$RDIR")
OUTFILE=defconfig_regen

# directory containing cross-compiler
TOOLCHAIN=$HOME/build/toolchain/bin/aarch64-linux-gnu-

export ARCH=arm64
export CROSS_COMPILE=$TOOLCHAIN

ABORT() {
	echo "Error: $*"
	exit 1
}

# selected device
[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || ABORT "No device specified"

# link device name to lg config files
if [ "$DEVICE" = "H850" ]; then
  DEVICE_DEFCONFIG=h1_global_com-perf_defconfig
elif [ "$DEVICE" = "H830" ]; then
  DEVICE_DEFCONFIG=h1_tmo_us-perf_defconfig
elif [ "$DEVICE" = "RS988" ]; then
  DEVICE_DEFCONFIG=h1_lra_us-perf_defconfig
elif [ "$DEVICE" = "H870" ]; then
  DEVICE_DEFCONFIG=lucye_global_com-perf_defconfig
elif [ "$DEVICE" = "US997" ]; then
  DEVICE_DEFCONFIG=lucye_nao_us-perf_defconfig
elif [ "$DEVICE" = "H872" ]; then
  DEVICE_DEFCONFIG=lucye_tmo_us-perf_defconfig
elif [ "$DEVICE" = "H990DS" ]; then
  DEVICE_DEFCONFIG=elsa_global_com-perf_defconfig
elif [ "$DEVICE" = "H990TR" ]; then
  DEVICE_DEFCONFIG=elsa_cno_cn-perf_defconfig
elif [ "$DEVICE" = "US996" ]; then
  DEVICE_DEFCONFIG=elsa_nao_us-perf_defconfig
elif [ "$DEVICE" = "US996Santa" ]; then
  DEVICE_DEFCONFIG=elsa_usc_us-perf_defconfig
elif [ "$DEVICE" = "LS997" ]; then
  DEVICE_DEFCONFIG=elsa_spr_us-perf_defconfig
elif [ "$DEVICE" = "VS995" ]; then
  DEVICE_DEFCONFIG=elsa_vzw-perf_defconfig
elif [ "$DEVICE" = "H918" ]; then
  DEVICE_DEFCONFIG=elsa_tmo_us-perf_defconfig
elif [ "$DEVICE" = "H910" ]; then
  DEVICE_DEFCONFIG=elsa_att_us-perf_defconfig
elif [ "$DEVICE" = "H915" ]; then
  DEVICE_DEFCONFIG=elsa_global_ca-perf_defconfig
elif [ "$DEVICE" = "F800K" ]; then
  DEVICE_DEFCONFIG=elsa_kt_kr-perf_defconfig
elif [ "$DEVICE" = "F800L" ]; then
  DEVICE_DEFCONFIG=elsa_lgu_kr-perf_defconfig
elif [ "$DEVICE" = "F800S" ]; then
  DEVICE_DEFCONFIG=elsa_skt_kr-perf_defconfig
fi

# check for stuff
[ -f "$RDIR/arch/$ARCH/configs/${DEVICE_DEFCONFIG}" ] \
	|| ABORT "$DEVICE_DEFCONFIG not found in $ARCH configs!"

[ -x "${CROSS_COMPILE}gcc" ] \
	|| ABORT "Cross-compiler not found at: ${CROSS_COMPILE}gcc"

cd "$RDIR" || ABORT "Failed to enter $RDIR!"

echo "Cleaning build..."
rm -rf build
mkdir build
make -s -i -C "$RDIR" O=build "$DEVICE_DEFCONFIG" menuconfig
echo "Showing differences between old config and new config"
echo "-----------------------------------------------------"
make -s -i -C "$RDIR" O=build "$DEVICE_DEFCONFIG"
if command -v colordiff >/dev/null 2>&1; then
	diff -Bwu --label "old config" build/.config --label "new config" build/.config.old | colordiff
else
	diff -Bwu --label "old config" build/.config --label "new config" build/.config.old
	echo "-----------------------------------------------------"
	echo "Consider installing the colordiff package to make diffs easier to read"
fi
echo "-----------------------------------------------------"
echo -n "Are you satisfied with these changes? Y/N: "
read option
case $option in
y|Y)
	cp build/.config.old "../$OUTFILE"
	echo "Copied new config to $OUTDIR/$OUTFILE"
	;;
*)
	echo "That's unfortunate"
	;;
esac
echo "Done."
