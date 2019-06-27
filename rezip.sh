#!/bin/bash
#
# Created by stendro. Based on build script by jcadduono.
#
# This script builds zips from the existing folders in the "OUTDIR" directory.
# This is so that you can make adjustments to the zip without recompiling
# the entire kernel. Changes are pulled from the source ("MK2DIR" directory),
# except tools or patches/ramdisk files.
#
# Specify the device, i.e ./rezip.sh H850

RDIR=$(pwd)

# color codes
COLOR_N="\033[0m"
COLOR_R="\033[0;31m"
COLOR_G="\033[1;32m"

ABORT() {
	echo -e $COLOR_R"Error: $*"
	exit 1
}

[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || ABORT "No device specified!"

VER=$(cat "${RDIR}/VERSION") \
		|| ABORT "No version file found in ${RDIR}"

OUTDIR=out
MK2DIR=${RDIR}/mk2000
INITRC_NAME=init.mktweaks.rc
BANNER_BETA=${MK2DIR}/banner-beta
DDIR=${RDIR}/${OUTDIR}/${DEVICE}
INIT_FILE_G6=${MK2DIR}/init-g6
INIT_FILE=${MK2DIR}/init
BANNER=${MK2DIR}/banner

[ -d "$DDIR" ] || ABORT "$DEVICE directory doesn't exist!"

CLEAN_ZIP() {
	echo "Remove old zip..."
	rm -f $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip
	rm -f $DDIR/ramdisk/*
}

COPY_AK() {
	echo "Copying AnyKernel3 files..."
	if grep -q 'BETA' $RDIR/VERSION; then
	  cp $BANNER_BETA $DDIR/banner \
		|| ABORT "Failed to copy banner"
	else
	  cp $BANNER $DDIR \
		|| ABORT "Failed to copy banner"
	fi
	source $MK2DIR/ak-template.sh > $DDIR/anykernel.sh \
		|| ABORT "Failed to generate *anykernel.sh*"
	cp $MK2DIR/update-binary $DDIR/META-INF/com/google/android \
		|| ABORT "Failed to copy *update-binary*"
}

COPY_INIT() {
	if [ "$DEVICE" = "H870" ] || [ "$DEVICE" = "US997" ] || [ "$DEVICE" = "H872" ]; then
	  echo "Copying init file (G6)..."
	  cp $INIT_FILE_G6 $DDIR/ramdisk/$INITRC_NAME \
		|| ABORT "Failed to copy init file"
	else
	  echo "Copying init file..."
	  cp $INIT_FILE $DDIR/ramdisk/$INITRC_NAME \
		|| ABORT "Failed to copy init file"
	fi
	echo "import /$INITRC_NAME" > $DDIR/patch/init_rc-mod \
		|| ABORT "Failed to make init_rc-mod"
}

ZIP_UP() {
	echo "Creating AnyKernel3 archive..."
	cd $DDIR
	zip -7qr $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip * \
		|| ABORT "Failed to create zip archive"
}

cd "$RDIR" || ABORT "Failed to enter ${RDIR}"
echo -e $COLOR_G"Preparing ${DEVICE} ${VER}"$COLOR_N

CLEAN_ZIP &&
COPY_AK &&
COPY_INIT &&
ZIP_UP &&
echo -e $COLOR_G"Finished! -- Look in *${OUTDIR}* folder."
