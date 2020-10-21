#!/bin/bash
#
# Thanks to Tkkg1994 and djb77 for the script
#
# MoRoKernel Build Script v1.2 modded by Arianoxx
#
# For
#
# EliteKernel
#

# SETUP
# -----
export ARCH=arm64
export SUBARCH=arm64
export GCC_DIR=aarch64-linux-gnu-
export CLANG_DIR=clang
export BUILD_JOB_NUMBER=`grep processor /proc/cpuinfo|wc -l`

export PLATFORM_VERSION=9.0.0

RDIR=$(pwd)
OUTDIR=$RDIR/arch/$ARCH/boot
DTSDIR=$RDIR/arch/$ARCH/boot/dts
DTBDIR=$OUTDIR/dtb
DTCTOOL=$RDIR/scripts/dtc/dtc
INCDIR=$RDIR/include
PAGE_SIZE=2048
DTB_PADDING=0

DEFCONFIG=herolte_defconfig

export K_NAME="Elite-Kernel"
export KBUILD_BUILD_VERSION="1"
S7DEVICE="OREO"
EDGE_LOG=Edge_build.log
FLAT_LOG=Flat_build.log
PORT=0


# FUNCTIONS
# ---------
FUNC_DELETE_PLACEHOLDERS()
{
	find . -name \.placeholder -type f -delete
        echo "Placeholders Deleted from Ramdisk"
        echo ""
}

FUNC_CLEAN_DTB()
{
	if ! [ -d $RDIR/arch/$ARCH/boot/dts ] ; then
		echo "no directory : "$RDIR/arch/$ARCH/boot/dts""
	else
		echo "rm files in : "$RDIR/arch/$ARCH/boot/dts/*.dtb""
		rm $RDIR/arch/$ARCH/boot/dts/*.dtb
		rm $RDIR/arch/$ARCH/boot/dtb/*.dtb
		rm $RDIR/arch/$ARCH/boot/boot.img-dtb
		rm $RDIR/arch/$ARCH/boot/boot.img-zImage
	fi
}

FUNC_BUILD_KERNEL()
{
	echo ""
        echo "build variant config="$MODEL ""

	cp -f $RDIR/arch/$ARCH/configs/$DEFCONFIG .config

	if [ "$MODEL" == "G935" ]; then
		scripts/configcleaner "
CONFIG_WLAN_HERO
CONFIG_WLAN_HERO2
CONFIG_FB_DSU_REG_LOCK
CONFIG_EXYNOS_DECON_LCD_MCD
CONFIG_LCD_ESD_IDLE_MODE
CONFIG_PANEL_S6E3HF4_WQHD
CONFIG_PANEL_S6E3HA3_DYNAMIC
CONFIG_SENSORS_SX9310_NORMAL_TOUCH_THRESHOLD
CONFIG_SENSORS_HERO2
"

		echo "
# CONFIG_WLAN_HERO is not set
CONFIG_WLAN_HERO2=y
CONFIG_FB_DSU_REG_LOCK=y
CONFIG_EXYNOS_DECON_LCD_MCD=y
# CONFIG_LCD_ESD_IDLE_MODE is not set
CONFIG_PANEL_S6E3HF4_WQHD=y
# CONFIG_PANEL_S6E3HA3_DYNAMIC is not set
# CONFIG_PANEL_DUALIZATION is not set
CONFIG_SENSORS_SX9310_NORMAL_TOUCH_THRESHOLD=168
CONFIG_SENSORS_HERO2=y
" >> .config
	fi

	if [ $ROM_VER == "10" ]; then
		scripts/configcleaner "
CONFIG_HALL_EVENT_REVERSE
"
		echo "
CONFIG_HALL_EVENT_REVERSE=y
" >> .config
	fi

	mv .config $RDIR/arch/$ARCH/configs/tmp_defconfig

	#FUNC_CLEAN_DTB

	make -j$BUILD_JOB_NUMBER ARCH=$ARCH \
			tmp_defconfig || exit -1

	if [ $CC_NAME == "clang" ]; then
		export KBUILD_COMPILER_STRING=$($CLANG_DIR --version | cut -f1 -d"-" | head -1)
		make -j$BUILD_JOB_NUMBER ARCH=$ARCH \
			CC=$CLANG_DIR \
			CLANG_TRIPLE=aarch64-linux-gnu- \
			CROSS_COMPILE=$GCC_DIR || exit -1
	elif [ $CC_NAME == "gcc" ]; then
		make -j$BUILD_JOB_NUMBER ARCH=$ARCH \
			CROSS_COMPILE=$GCC_DIR || exit -1
	fi

	echo ""

	rm -f $RDIR/arch/$ARCH/configs/tmp_defconfig
}

FUNC_BUILD_DTB()
{
	[ -f "$DTCTOOL" ] || {
		echo "You need to run ./build.sh first!"
		exit 1
	}
	case $MODEL in
	G930)
		DTSFILES="exynos8890-herolte_eur_open_04 exynos8890-herolte_eur_open_08
				exynos8890-herolte_eur_open_09 exynos8890-herolte_eur_open_10"
		;;
	G935)
		DTSFILES="exynos8890-hero2lte_eur_open_04 exynos8890-hero2lte_eur_open_08"
		;;
	*)
		echo "Unknown device: $MODEL"
		exit 1
		;;
	esac
	mkdir -p $OUTDIR $DTBDIR
	cd $DTBDIR || {
		echo "Unable to cd to $DTBDIR!"
		exit 1
	}
	rm -f ./*
	echo "Processing dts files."
	for dts in $DTSFILES; do
		echo "=> Processing: ${dts}.dts"
		${CROSS_COMPILE}cpp -nostdinc -undef -x assembler-with-cpp -I "$INCDIR" "$DTSDIR/${dts}.dts" > "${dts}.dts"
		echo "=> Generating: ${dts}.dtb"
		$DTCTOOL -p $DTB_PADDING -i "$DTSDIR" -O dtb -o "${dts}.dtb" "${dts}.dts"
	done
	echo "Generating dtb.img."
	$RDIR/scripts/dtbtool_exynos/dtbtool -o "$OUTDIR/dtb.img" -d "$DTBDIR/" -s $PAGE_SIZE
	echo "Done."
}

FUNC_BUILD_RAMDISK()
{
	echo ""
	echo "Building Ramdisk"
	mv $RDIR/arch/$ARCH/boot/Image $RDIR/arch/$ARCH/boot/boot.img-zImage
	mv $RDIR/arch/$ARCH/boot/dtb.img $RDIR/arch/$ARCH/boot/boot.img-dtb
	
	cd $RDIR/build
	mkdir temp
	cp -rf aik/. temp
	if [ $ROM_VER == "9" ]; then
		cp -rf ramdisk/. temp
	elif [ $ROM_VER == "10" ]; then
		cp -rf Q/. temp
	fi

	rm -f temp/split_img/boot.img-zImage
	rm -f temp/split_img/boot.img-dtb
	mv $RDIR/arch/$ARCH/boot/boot.img-zImage temp/split_img/boot.img-zImage
	mv $RDIR/arch/$ARCH/boot/boot.img-dtb temp/split_img/boot.img-dtb
	cd temp

	case $MODEL in
	G935)
		echo "Ramdisk for G935"
		;;
	G930)
		echo "Ramdisk for G930"

		sed -i 's/SRPOI30A000KU/SRPOI17A000KU/g' split_img/boot.img-board

		if [ $ROM_VER == "9" ]; then
			sed -i 's/G935/G930/g' p/default.prop
			sed -i 's/hero2/hero/g' p/default.prop
		fi
		;;
	esac

		echo "Done"

	./repackimg.sh

	cp -f image-new.img $RDIR/build
	cd ..
	rm -rf temp
	echo SEANDROIDENFORCE >> image-new.img
	mv image-new.img $MODEL-boot.img
}

FUNC_BUILD_FLASHABLES()
{
	cd $RDIR/build
	mkdir temp2
	cp -rf zip/common/. temp2
    	mv *.img temp2/
	cd temp2
	echo ""
	echo "Compressing kernels..."
	tar cv *.img | xz -9 > kernel.tar.xz
	mv kernel.tar.xz script/
	rm -f *.img

	zip -9 -r ../$ZIP_NAME *

	cd ..
    	rm -rf temp2

}



# MAIN PROGRAM
# ------------

MAIN()
{

(
	START_TIME=`date +%s`
	FUNC_DELETE_PLACEHOLDERS
	FUNC_BUILD_KERNEL
	FUNC_BUILD_DTB
	FUNC_BUILD_RAMDISK
	FUNC_BUILD_FLASHABLES
	END_TIME=`date +%s`
	let "ELAPSED_TIME=$END_TIME-$START_TIME"
	echo "Total compile time is $ELAPSED_TIME seconds"
	echo ""
) 2>&1 | tee -a ./$LOG

	echo "Your flasheable release can be found in the build folder"
	echo ""
}

MAIN2()
{

(
	START_TIME=`date +%s`
	FUNC_DELETE_PLACEHOLDERS
	FUNC_BUILD_KERNEL
	FUNC_BUILD_DTB
	FUNC_BUILD_RAMDISK
	END_TIME=`date +%s`
	let "ELAPSED_TIME=$END_TIME-$START_TIME"
	echo "Total compile time is $ELAPSED_TIME seconds"
	echo ""
) 2>&1 | tee -a ./$LOG

	echo "Your flasheable release can be found in the build folder"
	echo ""
}


# PROGRAM START
# -------------
clear
echo "**********************************"
echo "Select a compiler:"
echo "(1) GCC"
echo "(2) Clang"
read -p "Selected cross compiler: " ccprompt

if [ $ccprompt == "1" ]; then
    CC_NAME=gcc
    echo "
Using GCC

"
elif [ $ccprompt == "2" ]; then
    CC_NAME=clang
    echo "
Using clang

"
fi

echo "**********************************"
echo "Select ROM:"
echo "(1) OneUI 1.0"
echo "(2) OneUI 2.0"
read -p "Selected ROM: " romprompt

if [ $romprompt == "1" ]; then
     ROM_VER=9
     echo "
Compiling for OneUI 1.0
"

elif [ $romprompt == "2" ]; then
    ROM_VER=10
    echo "
Compiling for OneUI 2.0
"
fi

echo "**********************************"
echo "MoRoKernel & Arianoxx Build Script"
echo "**********************************"
echo ""
echo ""
echo "Build Kernel for:"
echo ""
echo "S7 Oreo"
echo "(1) S7 Flat SM-G930F"
echo "(2) S7 Edge SM-G935F"
echo "(3) S7 Edge + Flat"
echo ""
echo ""
read -p "Select an option to compile the kernel " prompt


if [ $prompt == "1" ]; then
    MODEL=G930
    DEVICE=$S7DEVICE
    LOG=$FLAT_LOG
    export KERNEL_VERSION="$K_NAME"
    echo "S7 Flat G930F Selected"
    ZIP_NAME=$K_NAME-$MODEL.zip
    MAIN
elif [ $prompt == "2" ]; then
    MODEL=G935
    DEVICE=$S7DEVICE
    LOG=$EDGE_LOG
    export KERNEL_VERSION="$K_NAME"
    echo "S7 Edge G935F Selected"
    ZIP_NAME=$K_NAME-$MODEL.zip
    MAIN
elif [ $prompt == "3" ]; then
    MODEL=G935
    DEVICE=$S7DEVICE
    LOG=$EDGE_LOG
    export KERNEL_VERSION="$K_NAME"
    echo "S7 EDGE + FLAT Selected"
    echo "Compiling EDGE ..."
    MAIN2
    MODEL=G930
    LOG=$FLAT_LOG
    export KERNEL_VERSION="$K_NAME-Oreo-$K_VERSION"
    echo "Compiling FLAT ..."
    ZIP_NAME=$K_NAME-G93X.zip
    MAIN
fi
