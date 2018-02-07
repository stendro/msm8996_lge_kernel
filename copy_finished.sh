#!/bin/bash
#
# Created by stendro. Based on build script by jcadduono.
#
# This script copies the dtb-embedded kernel, and modules, to the *out* directory.
# It also copies mk2000 specific AnyKernel2 files and creates a flashable zip.
# You can simply run ./copy_finished.sh after doing ./build.sh - it knows which device you built for.

RDIR=$(pwd)

ABORT() {
	echo "Error: $*"
	exit 1
}

DEVICE=$(cat "$RDIR/build/DEVICE") \
		|| ABORT "No device file found in $RDIR/build"

VER=$(cat "$RDIR/VERSION") \
		|| ABORT "No version file found in $RDIR"

DEVICE_FOLDER=$RDIR/out/$DEVICE
INIT_FILE=$RDIR/ramdisk/init
INIT_FILE_G6=$RDIR/ramdisk/init-g6

CLEAN_DIR() {
	echo "Cleaning folder..."
	rm -rf $DEVICE_FOLDER
	rm -f $RDIR/out/$DEVICE$VER.zip
}

SETUP_DIR() {
	echo "Setting up $DEVICE$VER..."
	unzip -q ramdisk/ak-root.zip -d $DEVICE_FOLDER \
		|| ABORT "Failed to unzip *ak-root.zip*"
	cp ramdisk/update-binary $DEVICE_FOLDER/META-INF/com/google/android \
		|| ABORT "Failed to copy *update-binary*"
}

COPY_AK() {
	echo "Copying AnyKernel2 file..."
	if ! grep -q 'BETA' $RDIR/VERSION; then
	  cp ramdisk/$DEVICE/anykernel.sh $DEVICE_FOLDER \
		|| ABORT "Failed to copy *anykernel.sh*"
	else
	  cp ramdisk/$DEVICE/anykernel-beta.sh $DEVICE_FOLDER/anykernel.sh \
		|| ABORT "Failed to copy *anykernel.sh*"
	fi
}

COPY_INIT() {
	echo "Copying init file..."
	if [ "$DEVICE" = "H870" ] || [ "$DEVICE" = "US997" ]; then
	  cp $INIT_FILE_G6 $DEVICE_FOLDER/ramdisk/init.blu_active.rc \
		|| ABORT "Failed to copy init file"
	else
	  cp $INIT_FILE $DEVICE_FOLDER/ramdisk/init.blu_active.rc \
		|| ABORT "Failed to copy init file"
	fi
}

COPY_KERNEL() {
	echo "Copying kernel and modules..."
	find build/lib/ -name '*.ko' -exec cp {} $DEVICE_FOLDER/modules \; \
		|| ABORT "Failed to copy modules"
	cp build/arch/arm64/boot/Image.lz4-dtb $DEVICE_FOLDER \
		|| ABORT "Failed to copy kernel"
}

ZIP_UP() {
	echo "Creating AnyKernel2 archive..."
	cd $DEVICE_FOLDER
	zip -qr $RDIR/out/$DEVICE$VER.zip * \
		|| ABORT "Failed to create zip archive"
}

cd "$RDIR" || ABORT "Failed to enter $RDIR"

CLEAN_DIR &&
SETUP_DIR &&
COPY_AK &&
COPY_INIT &&
COPY_KERNEL &&
ZIP_UP &&
echo "Finished!"
echo "Look in *out* folder"
