#!/bin/bash

## DEVICE STUFF
DEVICE_HARDWARE="sm8250"
DEVICE_MODEL="$1"
ARGS="$*"
ZIP_DIR="$(pwd)/AnyKernel3"

# Enviorment Variables
SRC_DIR="$(pwd)"
TC_DIR="$HOME/toolchains/neutron-clang"
JOBS="$(nproc --all)"
MAKE_PARAMS="-j$JOBS -C $SRC_DIR O=$SRC_DIR/out ARCH=arm64 CC=clang CLANG_TRIPLE=$TC_DIR/bin/aarch64-linux-gnu- LLVM=1 CROSS_COMPILE=$TC_DIR/bin/llvm-"
export PATH="$TC_DIR/bin:$PATH"

devicecheck() {
    if [ "$DEVICE_MODEL" == "r8q" ]; then
        DEVICE_NAME="r8q"
        DEFCONFIG=not_defconfig
    else
        echo "- Config not found"
        echo " Make sure first argument is DEVICE_MODEL"
        exit
    fi
}

toolchaincheck() {
    if [ -d "$TC_DIR" ]; then
        echo "Neutron Clang is already there"
        echo "Credits to dakkshesh07"
    else
        echo "Fetching Neutron Clang with antman script"
        echo "Credits to dakkshesh07"
        mkdir -p "$HOME/toolchains/neutron-clang"; cd "$HOME/toolchains/neutron-clang"; curl -LO "https://raw.githubusercontent.com/Neutron-Toolchains/antman/main/antman"; chmod +x antman; ./antman -S
        cd $SRC_DIR
    fi
}

copyoutputtozip() {
    cp ./out/arch/arm64/boot/Image ./AnyKernel3/
    cp ./out/arch/arm64/boot/dts/vendor/qcom/*.dtb ./AnyKernel3/a15.dtb
    cp ./out/arch/arm64/boot/dtbo.img ./AnyKernel3/
    cd AnyKernel3
    rm -rf not*
    zip -r9 $ZIP_NAME . -x '*.git*' '*patch*' '*ramdisk*' 'LICENSE' 'README.md'
    cd ..
}

help() {
    echo " "
    echo "How to use ?"
    echo " "
    echo " @$ bash ./build_script.sh {DEVICE_MODEL} --help"
    echo " "
    echo "Arguments:"
    echo "         --help    : this displays this message                            "
    echo " "
}
        echo "- Starting Building ..."
        devicecheck
        toolchaincheck
        make $MAKE_PARAMS $DEFCONFIG
        make $MAKE_PARAMS dtbs
        make $MAKE_PARAMS
        copyoutputtozip
        echo "This build was made using these arguments: $ARGS"
    fi
