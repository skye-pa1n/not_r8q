#!/bin/bash

LLVM_PATH="/home/skye/bomb/clang/bin/"
TC_PATH="/home/skye/bomb/clang/bin/"
GCC_PATH="/usr/bin/"
KERNEL_NAME=not-perf+
MAKE="./makeparallel"
BUILD_ENV="CC=${TC_PATH}clang CROSS_COMPILE=${GCC_PATH}aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 PATH=$LLVM_PATH:$LLD_PATH:$PATH"  
KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"

rm -rf /home/skye/bomb/out/arch/arm64/boot/Image
rm -rf AnyKernel3/dtb
rm -rf .version
rm -rf .local
#make O=/home/skye/bomb/out clean
#make O=/home/skye/bomb/out mrproper
make O=/home/skye/bomb/out ARCH=arm64 $BUILD_ENV not_defconfig

make -j$(nproc --all) O=/home/skye/bomb/out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV dtbs

DTB_OUT="/home/skye/bomb/out/arch/arm64/boot/dts/vendor/qcom"
cat $DTB_OUT/*.dtb > AnyKernel3/dtb

#make -j$(nproc --all) O=/home/skye/bomb/out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV Image
#IMAGEAOSP="/home/skye/bomb/out/arch/arm64/boot/Image"
#mv $IMAGEAOSP AnyKernel3/ImageAOSP

rm -rf /home/skye/bomb/out/arch/arm64/boot/Image
rm -rf .version
rm -rf .local
make O=/home/skye/bomb/out clean
make O=/home/skye/bomb/out mrproper
make O=/home/skye/bomb/out ARCH=arm64 $BUILD_ENV not_ui_defconfig

make -j$(nproc --all) O=/home/skye/bomb/out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV Image
IMAGEUI="/home/skye/bomb/out/arch/arm64/boot/Image"
mv $IMAGEUI AnyKernel3/ImageUI
rm -rf /home/skye/bomb/out/arch/arm64/boot/Image

cd AnyKernel3
rm *.zip
zip -r9 ${KERNEL_NAME}.zip .
echo "The bomb has been planted."

