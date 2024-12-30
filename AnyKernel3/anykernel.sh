# AnyKernel3 Ramdisk Mod Script
# osm0sis @ xda-developers

## AnyKernel setup
# begin properties
properties() { '
kernel.string=not_Kernel by @skye
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=r8q
device.name2=r8qxx
device.name3=r8qxxx
device.name4=e3q
device.name5=e3qxxx
supported.versions=13 - 15
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

# begin dtb changes

android=$(file_getprop /system/build.prop ro.system.build.version.release);
if [ $android == 14 ]; then
    ui_print " "
    ui_print " • Using A14 Device Tree Blob • "
    mv $home/a14.dtb $home/dtb
elif [ $android == 15 ]; then
    ui_print " "
    ui_print " • Using A15 Device Tree Blob • "
    mv $home/a15.dtb $home/dtb
elif [ $android == 13 ]; then
    ui_print " "
    ui_print " • Using A13 Device Tree Blob • "
    mv $home/a13.dtb $home/dtb
fi

# end dtb changes

# begin cmdline-ksu changes

case "$ZIPFILE" in
   *-noksu*)
    ui_print " "
    ui_print " • Disable KSU Option detected • "
    ui_print " "
    ui_print " • Disabling KSU • "
    patch_cmdline "enable_kernelsu" "enable_kernelsu=0";
    ;;
   *)
    ui_print " "
    ui_print " • Default KSU Option detected • "
    ui_print " "
    ui_print " • Enabling KSU • "
    patch_cmdline "enable_kernelsu" "enable_kernelsu=1";
    ;;
esac

#end cmdline-ksu changes

# begin cmdline changes

oneui=$(file_getprop /system/build.prop ro.build.version.oneui);
gsi=$(file_getprop /system/build.prop ro.product.system.device);
if [ -n "$oneui" ]; then
   ui_print " "
   ui_print " • OneUI ROM detected! Proceed with Caution! • "
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
elif [ $gsi == "generic" ]; then
   ui_print " "
   ui_print " • GSI ROM detected! Proceed with Caution! • "
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=0";
else
   ui_print " "
   ui_print " • AOSP ROM detected! • "
   ui_print " "
   ui_print " • Patching CMDline... • "
   patch_cmdline "androidboot.verifiedbootstate=orange" "androidboot.verifiedbootstate=green"
   ui_print " "
   ui_print " • Patching Props... • "
   resetprop -n ro.boot.vbmeta.avb_version 4.0
   resetprop -n ro.boot.vbmeta.device_state locked
   resetprop -n ro.boot.vbmeta.hash_alg sha256
   resetprop -n ro.boot.vbmeta.invalidate_on_error yes
   resetprop -n ro.boot.vbmeta.size 17472
   resetprop -n ro.boot.vbmeta.device_state locked
   resetprop -n ro.boot.verifiedbootstate green
   resetprop -n ro.boot.flash.locked 1
   resetprop -n ro.boot.veritymode enforcing
   resetprop -n ro.boot.warranty_bit 0
   resetprop -n ro.warranty_bit 0
   resetprop -n ro.debuggable 0
   resetprop -n ro.force.debuggable 0
   resetprop -n ro.secure 1
   resetprop -n ro.adb.secure 1
   resetprop -n ro.build.type user
   resetprop -n ro.build.tags release-keys
   resetprop -n ro.vendor.boot.warranty_bit 0
   resetprop -n ro.vendor.warranty_bit 0
   resetprop -n vendor.boot.vbmeta.device_state locked
   resetprop -n ro.secureboot.lockstate locked
   resetprop -n vendor.boot.verifiedbootstate green
   ui_print " "
   ui_print " • Patching Fingerprint Sensor... • "
   patch_cmdline "android.is_aosp" "android.is_aosp=1";
fi

# end cmdline changes

write_boot;
## end boot install
exit 0
