#!/bin/bash

LLVM_PATH="/home/skye/clang/clang/bin/"
TC_PATH="/home/skye/clang/clang/bin/"
GCC_PATH="/usr/bin/"
KERNEL_NAME=not-pa1n!
MAKE="./makeparallel"
BUILD_ENV="CC=${TC_PATH}clang CROSS_COMPILE=${GCC_PATH}aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 PATH=$LLVM_PATH:$LLD_PATH:$PATH"  

KERNEL_MAKE_ENV="DTC_EXT=$(pwd)/tools/dtc CONFIG_BUILD_ARM64_DT_OVERLAY=y"
rm -rf AnyKernel3/dtb
make clean
rm -rf .version
rm -rf .local
make mrproper
make O=out clean
make O=out mrproper
make O=out ARCH=arm64 $BUILD_ENV not_defconfig

DATE_START=$(date +"%s")

make -j$(nproc --all) O=out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV Image

make -j$(nproc --all) O=out ARCH=arm64 $KERNEL_MAKE_ENV $BUILD_ENV dtbs

DTB_OUT="out/arch/arm64/boot/dts/vendor/qcom"
IMAGE="out/arch/arm64/boot/Image"

cat $DTB_OUT/*.dtb > AnyKernel3/dtb

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Tempo de compilação: $(($DIFF / 60)) minutos(s) and $(($DIFF % 60)) segundos."

cp $IMAGE AnyKernel3/Image
cd AnyKernel3
rm *.zip
zip -r9 ${KERNEL_NAME}.zip .
