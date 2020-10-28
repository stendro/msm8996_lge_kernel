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
BDATE=$(LC_ALL='en_US.utf8' date '+%b %d %Y')
GBRANCH=$(git rev-parse --abbrev-ref HEAD)
GREPO=$(git remote get-url origin)

# used to auto-generate an additional single-sim zip
# from h990ds kernel. See "H990_SIM" below.
if [ "$DEVICE" = "H990DS" ] && [ "$1" = "SS" ]; then
  DEVICE=H990SS
fi

OUTDIR=out
MK2DIR=${RDIR}/mk2000
GITCOM=${BDIR}/GITCOMMITS
MOD_DIR=${BDIR}/lib/modules
INITRC_NAME=init.mktweaks.rc
MDIR=modules
KERN_DIR=${BDIR}/arch/arm64/boot
BANNER_BETA=${MK2DIR}/banner-beta
DDIR=${RDIR}/${OUTDIR}/${DEVICE}
INIT_FILE_G6=${MK2DIR}/init-g6
INIT_FILE=${MK2DIR}/init
BANNER=${MK2DIR}/banner

if [ $# -eq 1 ] ; then
TARGET=$1	
fi

CLEAN_DIR() {
	echo "Cleaning folder..."
	rm -rf $DDIR
	if [ "$TARGET" = "nethunter" ]; then
		rm -f $RDIR/$OUTDIR/${DEVICE}_${TARGET}_${VER}-mk2000.zip
	else
		rm -f $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip
	fi
}

SETUP_DIR() {
	echo "Setting up folder..."
	mkdir -p $RDIR/$OUTDIR
	unzip -q $MK2DIR/ak-root.zip -d $DDIR \
		|| ABORT "Failed to unzip *ak-root.zip*"
	cp $MK2DIR/update-binary $DDIR/META-INF/com/google/android \
		|| ABORT "Failed to copy *update-binary*"
	[ -e "$GITCOM" ] && cp $GITCOM $DDIR/gitlog &&
	sed -i '1iVersion: '$VER'\nBuild date: '"$BDATE"'\n'$GREPO'/commits/'$GBRANCH'\
		\nThe last 50 commits:\n' $DDIR/gitlog \
		|| echo -e $COLOR_R"Failed to create gitlog"$COLOR_G "Continuing..."$COLOR_N
}

COPY_AK() {
	echo "Copying AnyKernel2 files..."
	if grep -q 'BETA' $RDIR/VERSION; then
	  cp $BANNER_BETA $DDIR/banner \
		|| ABORT "Failed to copy banner"
	  echo "  ${BVER}" > $DDIR/version
	else
	  cp $BANNER $DDIR \
		|| ABORT "Failed to copy banner"
	  echo "  ${VER}" > $DDIR/version
	fi
	source $MK2DIR/ak-template.sh > $DDIR/anykernel.sh \
		|| ABORT "Failed to generate *anykernel.sh*"
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
	echo "Copying init.d..."
	cp ${MK2DIR}/init.d $DDIR/patch/ \
		|| ABORT "Failed to copy init file"
	echo "import /$INITRC_NAME" > $DDIR/patch/init_rc-mod \
		|| ABORT "Failed to make init_rc-mod"
}

COPY_KERNEL() {
	echo "Copying kernel..."
	cp $KERN_DIR/Image.${COMP}-dtb $DDIR \
		|| ABORT "Failed to copy kernel"
	if grep -q 'CONFIG_MODULES=y' $BDIR/.config; then
	  echo "Copying modules..."
	  find $MOD_DIR/ -name '*.ko' -exec cp {} $DDIR/$MDIR \; \
		|| ABORT "Failed to copy modules"
	fi
	if [ "$TARGET" = "nethunter" ]; then
	  echo "Copying modules nethunter"
	  rsync -r --no-links $MOD_DIR/* $DDIR/modules/
	fi
}

ZIP_UP() {
	echo "Creating AnyKernel2 archive..."
	cd $DDIR
	if [ "$TARGET" = "nethunter" ]; then
	  zip -7qr $RDIR/$OUTDIR/${DEVICE}_${TARGET}_${VER}-mk2000.zip * \
		|| ABORT "Failed to create zip archive"
	else
	zip -7qr $RDIR/$OUTDIR/${DEVICE}_${VER}-mk2000.zip * \
		|| ABORT "Failed to create zip archive"
	fi
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
