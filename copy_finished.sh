#!/bin/bash
#
# Created by stendro. Based on build script by jcadduono.
#
# This script copies the dtb-embedded kernel, and modules, to the "OUTDIR" directory.
# It also copies mk2000 specific AnyKernel2 files and creates a flashable zip.
# You can simply run ./copy_finished.sh after doing ./build.sh - it knows which device you built for.

RDIR=$(pwd)
BDIR=${RDIR}/build

# color codes
COLOR_N="\033[0m"
COLOR_R="\033[0;31m"
COLOR_G="\033[1;32m"

ABORT() {
	echo -e $COLOR_R"Error: $*"
	exit 1
}

DEVICE=$(cat "${BDIR}/DEVICE") \
		|| ABORT "No device file found in ${BDIR}"

VER=$(cat "${RDIR}/VERSION") \
		|| ABORT "No version file found in ${RDIR}"

COMP=$(cat "${BDIR}/COMPRESSION") \
		|| ABORT "No compression file found in ${BDIR}"

BVER=$(cat ${RDIR}/VERSION | cut -f1 -d'-')

# used to auto-generate an additional single-sim zip
# from h990ds kernel. See "H990_SIM" below.
if [ "$DEVICE" = "H990DS" ] && [ "$1" = "SS" ]; then
  DEVICE=H990SS
fi

OUTDIR=out
RDISK=${RDIR}/mk2000
AK_DIR=${RDISK}/ak-script
MOD_DIR=${BDIR}/lib/modules
KERN_DIR=${BDIR}/arch/arm64/boot
BANNER_BETA=${RDISK}/banner-beta
DDIR=${RDIR}/${OUTDIR}/${DEVICE}
INIT_FILE_G6=${RDISK}/init-g6
INIT_FILE=${RDISK}/init
BANNER=${RDISK}/banner

CLEAN_DIR() {
	echo "Cleaning folder..."
	rm -rf $DDIR
	rm -f $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip
}

SETUP_DIR() {
	echo "Setting up folder..."
	mkdir -p $RDIR/$OUTDIR
	unzip -q $RDISK/ak-root.zip -d $DDIR \
		|| ABORT "Failed to unzip *ak-root.zip*"
	cp $RDISK/update-binary $DDIR/META-INF/com/google/android \
		|| ABORT "Failed to copy *update-binary*"
}

COPY_AK() {
	echo "Copying AnyKernel2 files..."
	if grep -q 'BETA' $RDIR/VERSION; then
	  cp $BANNER_BETA $DDIR/banner \
		|| ABORT "Failed to copy banner"
	  echo "  ${BVER} Oreo" > $DDIR/version
	else
	  cp $BANNER $DDIR \
		|| ABORT "Failed to copy banner"
	  echo "  ${VER} Oreo" > $DDIR/version
	fi
	cp $AK_DIR/anykernel-${DEVICE}.sh $DDIR/anykernel.sh \
		|| ABORT "Failed to copy *anykernel.sh*"
}

COPY_INIT() {
	if [ "$DEVICE" = "H870" ] || [ "$DEVICE" = "US997" ] || [ "$DEVICE" = "H872" ]; then
	  echo "Copying init file (G6)..."
	  cp $INIT_FILE_G6 $DDIR/ramdisk/init.blu_active.rc \
		|| ABORT "Failed to copy init file"
	else
	  echo "Copying init file..."
	  cp $INIT_FILE $DDIR/ramdisk/init.blu_active.rc \
		|| ABORT "Failed to copy init file"
	fi
}

COPY_KERNEL() {
	echo "Copying kernel..."
	cp $KERN_DIR/Image.${COMP}-dtb $DDIR \
		|| ABORT "Failed to copy kernel"
	if grep -q 'CONFIG_MODULES=y' $BDIR/.config; then
	  echo "Copying modules..."
	  find $MOD_DIR/ -name '*.ko' -exec cp {} $DDIR/modules \; \
		|| ABORT "Failed to copy modules"
	fi
}

ZIP_UP() {
	echo "Creating AnyKernel2 archive..."
	cd $DDIR
	zip -7qr $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip * \
		|| ABORT "Failed to create zip archive"
}

H990_SIM() {
	if [ "$DEVICE" = "H990DS" ]; then
	  cd $RDIR
	  exec bash "$0" "SS"
	fi
}

cd "$RDIR" || ABORT "Failed to enter ${RDIR}"
echo -e $COLOR_G"Preparing ${DEVICE} ${VER}"$COLOR_N

CLEAN_DIR &&
SETUP_DIR &&
COPY_AK &&
COPY_INIT &&
COPY_KERNEL &&
ZIP_UP &&
H990_SIM &&
echo -e $COLOR_G"Finished! -- Look in *${OUTDIR}* folder."
