# ------------------------------
# TGPKERNEL INSTALLER 4.11.8
#
# Anykernel2 created by @osm0sis
# Everything else done by @djb77
# ------------------------------

## AnyKernel setup
properties() {
kernel.string=
do.devicecheck=1
do.modules=0
do.cleanup=1
do.cleanuponabort=1
device.name1=herolte
device.name2=hero2lte
device.name3=
device.name4=
device.name5=
}

# Shell Variables
block=/dev/block/platform/155a0000.ufs/by-name/BOOT
ramdisk=/tmp/anykernel/ramdisk
split_img=/tmp/anykernel/split_img
patch=/tmp/anykernel/patch
is_slot_device=0
ramdisk_compression=auto

# Extra 0's needed for CPU Freqs
ZEROS=000

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. /tmp/anykernel/tools/ak2-core.sh

## AnyKernel install
ui_print "- Extracing Boot Image"
dump_boot

# Ramdisk changes - Set split_img OSLevel depending on ROM
(grep -w ro.build.version.security_patch | cut -d= -f2) </system/build.prop > /tmp/rom_oslevel
ROM_OSLEVEL=`cat /tmp/rom_oslevel`
echo $ROM_OSLEVEL | rev | cut -c4- | rev > /tmp/rom_oslevel
ROM_OSLEVEL=`cat /tmp/rom_oslevel`
ui_print "- Setting security patch level to $ROM_OSLEVEL"
echo $ROM_OSLEVEL > $split_img/boot.img-oslevel

# Ramdisk changes - Deodexed ROM
if egrep -q "install=1" "/tmp/aroma/deodexed.prop"; then
	ui_print "- Patching for Deodexed ROM"
	replace_string default.prop "pm.dexopt.first-boot=interpret-only" "pm.dexopt.first-boot=quicken" "pm.dexopt.first-boot=interpret-only"
	replace_string default.prop "pm.dexopt.boot=verify-profile" "pm.dexopt.boot=verify" "pm.dexopt.boot=verify-profile"
	replace_string default.prop "pm.dexopt.install=interpret-only" "pm.dexopt.install=quicken" "pm.dexopt.install=interpret-only"
	cp -rf $patch/sepolicy/* $ramdisk
	chmod 644 $ramdisk/sepolicy
fi

# Ramdisk changes - Insecure ADB
if egrep -q "install=1" "/tmp/aroma/insecureadb.prop"; then
	ui_print "- Enabling Insecure ADB"
	cp -rf $patch/adbd/* $ramdisk
	chmod 755 $ramdisk/sbin/adbd
	replace_string default.prop "ro.adb.secure=0" "ro.adb.secure=1" "ro.adb.secure=0"
fi

# Ramdisk changes - Spectrum
if egrep -q "install=1" "/tmp/aroma/spectrum.prop"; then
	ui_print "- Adding Spectrum"
	cp -rf $patch/spectrum/* $ramdisk
	chmod 644 $ramdisk/init.spectrum.rc
	chmod 644 $ramdisk/init.spectrum.sh
	insert_line init.rc "import /init.spectrum.rc" after "import /init.services.rc" "import /init.spectrum.rc"
fi

# Ramdisk changes - PWMFix
if egrep -q "install=1" "/tmp/aroma/pwm.prop"; then
	ui_print "- Enabling PWMFix by default"
	replace_string sbin/tgpkernel.sh "echo \"1\" > /sys/class/lcd/panel/smart_on" "echo \"0\" > /sys/class/lcd/panel/smart_on" "echo \"1\" > /sys/class/lcd/panel/smart_on"
fi

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
chmod 644 $ramdisk/default.prop
chmod 755 $ramdisk/init.rc
chmod 755 $ramdisk/sbin/tgpkernel.sh
chown -R root:root $ramdisk/*

# End ramdisk changes
ui_print "- Writing Boot Image"
write_boot

## End install
ui_print "- Done"

