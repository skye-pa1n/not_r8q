# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=not_Kernel by @skye // pa1n
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=r8q
device.name2=r8qxx
device.name3=r8qxxx
supported.versions=11 - 15
supported.patchlevels=
'; } # end properties

# shell variables
block=/dev/block/platform/soc/1d84000.ufshc/by-name/boot;
is_slot_device=0;
ramdisk_compression=auto;

## AnyKernel methods (DO NOT CHANGE)
# import patching functions/variables - see for reference
. tools/ak3-core.sh;

## AnyKernel file attributes
# set permissions/ownership for included ramdisk files
set_perm_recursive 0 0 755 644 $ramdisk/*;
set_perm_recursive 0 0 750 750 $ramdisk/init* $ramdisk/sbin;

## AnyKernel boot install
dump_boot;

# Flash DTBO

flash_dtbo() {
  if [ -f $ZIPFILE/dtbo.img ]; then
    dd if=$ZIPFILE/dtbo.img of=/dev/block/by-name/dtbo
  fi
}
# begin kernel/dtb/dtbo changes
android=$(file_getprop /system/build.prop ro.system.build.version.release);
oneui=$(file_getprop /system/build.prop ro.build.version.oneui);
gsi=$(file_getprop /system/build.prop ro.product.system.device);
if [ $oneui == 60101 ]; then
   ui_print " "
   ui_print " • OneUI 6.1.1 ROM detected! • "
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
   flash_dtbo
elif [ $oneui == 60100 ]; then
   ui_print " "
   ui_print " • OneUI 6.1 ROM detected! • "
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
   elif [ $oneui == 50100 ]; then
   ui_print " "
   ui_print " • OneUI 5.1 ROM detected! • "
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
elif [ -n "$oneui" ]; then
   ui_print " "
   ui_print " • Legacy OneUI ROM detected! • " # ie. OneUI 5.0/4.1/4.0/3.1
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
elif [ $gsi == generic ]; then
   ui_print " "
   ui_print " • GSI ROM detected! • "
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
elif [ $android == 14 ]; then
   ui_print " "
   ui_print " • AOSP A14 ROM detected! • "
   ui_print " "
   ui_print " • Patching CMDline... • "
   patch_cmdline "androidboot.verifiedbootstate=orange" "androidboot.verifiedbootstate=green"
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=1";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
elif [ $android == 15 ]; then
   ui_print " "
   ui_print " • AOSP A15 ROM detected! • "
   ui_print " "
   ui_print " • Patching CMDline... • "
   patch_cmdline "androidboot.verifiedbootstate=orange" "androidboot.verifiedbootstate=green"
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=1";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
   patch_cmdline "android.is_aosp" "android.is_aosp=1";
else
   ui_print " "
   ui_print " • AOSP A13 ROM detected! • "
   ui_print " "
   ui_print " • Patching CMDline... • "
   patch_cmdline "androidboot.verifiedbootstate=orange" "androidboot.verifiedbootstate=green"
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=1";
   ui_print " "
   ui_print " • Flashing Custom Device Tree Blob Overlay • "
fi

# end kernel/dtb/dtbo changes

write_boot;
## end boot install
