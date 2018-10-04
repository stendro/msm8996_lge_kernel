#!/bin/bash
#
# Created by stendro. Based on build script by jcadduono.
#
# This script builds zips from the existing folders in the "out" directory.
# This is so that you can make adjustments to the zip without recompiling
# the entire kernel. Specify the device, i.e ./build_zip.sh H850

RDIR=$(pwd)

ABORT() {
	echo "Error: $*"
	exit 1
}

[ "$1" ] && DEVICE=$1
[ "$DEVICE" ] || ABORT "No device specified"

VER=$(cat "${RDIR}/VERSION") \
		|| ABORT "No version file found in ${RDIR}"

BVER=$(cat ${RDIR}/VERSION | cut -f1 -d'-')

RDISK=${RDIR}/mk2000
AK_DIR=${RDISK}/ak-script
BANNER_BETA=${RDISK}/banner-beta
INIT_FILE_G6=${RDISK}/init-g6
DDIR=${RDIR}/out/${DEVICE}
INIT_FILE=${RDISK}/init
BANNER=${RDISK}/banner

CLEAN_ZIP() {
	echo "Remove old zip..."
	rm -f $RDIR/out/${DEVICE}_${VER}-mk2000.zip
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
	cp $RDISK/update-binary $DDIR/META-INF/com/google/android \
		|| ABORT "Failed to copy *update-binary*"
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

ZIP_UP() {
	echo "Creating AnyKernel2 archive..."
	cd $DDIR
	zip -7qr $RDIR/out/${DEVICE}_${VER}-mk2000.zip * \
		|| ABORT "Failed to create zip archive"
}

cd "$RDIR" || ABORT "Failed to enter ${RDIR}"
echo "Preparing ${DEVICE} ${VER}"

CLEAN_ZIP &&
COPY_AK &&
COPY_INIT &&
ZIP_UP &&
echo "Finished!"
echo "Look in *out* folder"
