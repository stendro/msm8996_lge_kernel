#!/bin/bash
#
# Created by stendro. Based on build script by jcadduono.
#
# This script copies the dtb-embedded kernel, and modules, to the *out* directory.
# It also copies mk2000 specific AnyKernel2 files and creates a flashable zip.
# You can simply run ./copy_finished.sh after doing ./build.sh - it knows which device you built for.

RDIR=$(pwd)
BDIR=${RDIR}/build

ABORT() {
	echo "Error: $*"
	exit 1
}

DEVICE=$(cat "${BDIR}/DEVICE") \
		|| ABORT "No device file found in ${BDIR}"

VER=$(cat "${RDIR}/VERSION") \
		|| ABORT "No version file found in ${RDIR}"

BVER=$(cat ${RDIR}/VERSION | cut -f1 -d'-')

RDISK=${RDIR}/mk2000
AK_DIR=${RDISK}/ak-script
MOD_DIR=${BDIR}/lib/modules
KERN_DIR=${BDIR}/arch/arm64/boot
BANNER_BETA=${RDISK}/banner-beta
INIT_FILE_G6=${RDISK}/init-g6
DDIR=${RDIR}/out/${DEVICE}
INIT_FILE=${RDISK}/init
BANNER=${RDISK}/banner

CLEAN_DIR() {
	echo "Cleaning folder..."
	rm -rf $DDIR
	rm -f $RDIR/out/${DEVICE}_${VER}-mk2000.zip
}

SETUP_DIR() {
	echo "Setting up folder..."
	mkdir -p $RDIR/out
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
	  echo "  $BVER" > $DDIR/version
	else
	  cp $BANNER $DDIR \
		|| ABORT "Failed to copy banner"
	  echo "  $VER" > $DDIR/version
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
	if [ "$DEVICE" = "H850" ] || [ "$DEVICE" = "H830" ] || [ "$DEVICE" = "RS988" ]; then
	  echo "Copying kernel and modules (G5)..."
	  cp $KERN_DIR/Image.lz4 $DDIR \
		|| ABORT "Failed to copy kernel"
	  python $RDISK/dtbTool -o $DDIR/dt.img -s 4096 $KERN_DIR/dts \
		|| ABORT "Failed to create dt.img - do you have libfdt installed?"
	else
	  echo "Copying kernel and modules..."
	  cp $KERN_DIR/Image.lz4-dtb $DDIR \
		|| ABORT "Failed to copy kernel"
	fi
	find $MOD_DIR/ -name '*.ko' -exec cp {} $DDIR/modules \; \
		|| ABORT "Failed to copy modules"
}

ZIP_UP() {
	echo "Creating AnyKernel2 archive..."
	cd $DDIR
	zip -7qr $RDIR/out/${DEVICE}_${VER}-mk2000.zip * \
		|| ABORT "Failed to create zip archive"
}

cd "$RDIR" || ABORT "Failed to enter ${RDIR}"
echo "Preparing ${DEVICE} ${VER}"

CLEAN_DIR &&
SETUP_DIR &&
COPY_AK &&
COPY_INIT &&
COPY_KERNEL &&
ZIP_UP &&
echo "Finished!"
echo "Look in *out* folder"
