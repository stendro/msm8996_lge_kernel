#!/bin/bash
#
# Created by stendro. Based on build script by jcadduono.
#
# This script builds zips from the existing folders in the "OUTDIR" directory.
# This is so that you can make adjustments to the zip without recompiling
# the entire kernel. Changes are pulled from the source ("RDISK" directory),
# except tools or patches/ramdisk files.
#
# Specify the device, i.e ./rezip.sh H918

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
RDISK=${RDIR}/mk2000
AK_DIR=${RDISK}/ak-script
BANNER_BETA=${RDISK}/banner-beta
DDIR=${RDIR}/${OUTDIR}/${DEVICE}
INIT_FILE=${RDISK}/init
BANNER=${RDISK}/banner

[ -d "$DDIR" ] || ABORT "$DEVICE directory doesn't exist!"

CLEAN_ZIP() {
	echo "Remove old zip..."
	rm -f $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip
}

COPY_AK() {
	echo "Copying AnyKernel2 files..."
	if grep -q 'BETA' $RDIR/VERSION; then
	  cp $BANNER_BETA $DDIR/banner \
		|| ABORT "Failed to copy banner"
	else
	  cp $BANNER $DDIR \
		|| ABORT "Failed to copy banner"
	fi
	cp $AK_DIR/anykernel-${DEVICE}.sh $DDIR/anykernel.sh \
		|| ABORT "Failed to copy *anykernel.sh*"
	cp $RDISK/update-binary $DDIR/META-INF/com/google/android \
		|| ABORT "Failed to copy *update-binary*"
}

COPY_INIT() {
	  echo "Copying init file..."
	  cp $INIT_FILE $DDIR/ramdisk/init.blu_active.rc \
		|| ABORT "Failed to copy init file"
}

ZIP_UP() {
	echo "Creating AnyKernel2 archive..."
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
