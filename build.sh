#! /usr/bin/env bash

#
# Rissu Kernel Project
#

DEVICE="S20 FE"

setconfig() { # fmt: setconfig enable/disable <CONFIG_NAME>
	if [ -d $(pwd)/scripts ]; then
		./scripts/config --file ./out/.config --`echo $1` CONFIG_`echo $2`
	else
		echo -e "Folder scripts not found!"
		exit
	fi
}

# generate simple c file
if [ ! -e utsrelease.c ]; then
echo "/* Generated file by `basename $0` */
#include <stdio.h>
#include \"out/include/generated/utsrelease.h\"

char utsrelease[] = UTS_RELEASE;

int main() {
	printf(\"%s\n\", utsrelease);
	return 0;
}" > utsrelease.c
fi

usage() {
	echo -e "Usage: bash `basename $0` <build_target> <-j | --jobs> <(job_count)> <defconfig>"
	printf "\tbuild_target: kernel, config\n"
	printf "\t-j or --jobs: <int>\n"
	printf "\tavailable defconfig: `ls arch/arm64/configs`\n"
	echo ""
	printf "NOTE: Run: \texport CROSS_COMPILE=\"<PATH_TO_ANDROID_CC>\"\n"
	printf "\t\texport PATH=\"<PATH_TO_LLVM>\"\n"
	printf "before running this script!\n"
	echo ""
	printf "Misc:\n"
	printf "\tPOST_BUILD_CLEAN: Clean post build (host only)\n"
	printf "\tLTO: Use Link-time Optimization; options: (none, thin, full)\n"
	exit
}

if [ $1 = "clean" ]; then
	if [ -d $(pwd)/out ]; then
		rm -rf out
		exit
	elif [ -f $(pwd)/.config ]; then
		make clean
		make mrproper
		exit
	else
		echo -e "All clean."
		exit
	fi
else
	if [ $# != 4 ]; then
		usage;
	fi
fi

pr_invalid() {
	echo -e "Invalid args: $@"
	exit
}

BUILD_TARGET="$1"
FIRST_JOB="$2"
JOB_COUNT="$3"
DEFCONFIG="$4"

if [ "$BUILD_TARGET" = "kernel" ]; then
	BUILD="kernel"
elif [ "$BUILD_TARGET" = "defconfig" ]; then
	BUILD="defconfig"
else
	pr_invalid $1
fi

if [ "$FIRST_JOB" = "-j" ] || [ "$FIRST_JOB" = "--jobs" ]; then
	if [ ! -z $JOB_COUNT ]; then
		ALLOC_JOB=$JOB_COUNT
	else
		pr_invalid $3
	fi
else
	pr_invalid $2
fi

if [ ! -z "$DEFCONFIG" ]; then
	BUILD_DEFCONFIG="$DEFCONFIG"
else
	pr_invalid $4
fi

DEFAULT_ARGS="
CONFIG_SECTION_MISMATCH_WARN_ONLY=y
ARCH=arm64
"
IMAGE="$(pwd)/out/arch/arm64/boot/Image"
AK3="$(pwd)/AnyKernel3"

if [ "$LLVM" = "1" ]; then
	LLVM_="true"
	DEFAULT_ARGS+=" LLVM=1"
	export LLVM=1
	if [ "$LLVM_IAS" = "1" ]; then
		LLVM_IAS_="true"
		DEFAULT_ARGS+=" LLVM_IAS=1"
		export LLVM_IAS=1
	fi
else
	LLVM_="false"
	if [ "$LLVM_IAS" != "1" ]; then
		LLVM_IAS_="false"
	fi
fi

export ARCH=arm64
export CLANG_TRIPLE=aarch64-linux-gnu-

pr_sum() {
	if [ -z $KBUILD_BUILD_USER ]; then
		KBUILD_BUILD_USER="`whoami`"
	fi
	if [ -z $KBUILD_BUILD_HOST ]; then
		KBUILD_BUILD_HOST="`hostname`"
	fi
	
	echo ""
	echo -e "Host Arch: `uname -m`"
	echo -e "Host Kernel: `uname -r`"
	echo -e "Host gnumake version: `make -v | grep -e "GNU Make"`"
	echo ""
	echo -e "Linux version: `make kernelversion`"
	echo -e "Kernel builder user: $KBUILD_BUILD_USER"
	echo -e "Kernel builder host: $KBUILD_BUILD_HOST"
	echo -e "Build date: `date`"
	echo -e "Build target: `echo $BUILD`"
	echo -e "Arch: $ARCH"
	echo -e "Defconfig: $BUILD_DEFCONFIG"
	echo -e "Allocated core: $ALLOC_JOB"
	echo ""
	echo -e "LLVM: $LLVM_"
	echo -e "LLVM_IAS: $LLVM_IAS_"
	echo ""
	echo -e "LTO: $LTO"
	echo ""
}

# call summary
pr_sum

pr_post_build() {
	echo ""
	echo -e "## Build $@ at `date` ##"
	echo ""
	
	if [ "$@" = "failed" ]; then
		exit
	fi
}

post_build_clean() {
	rm $AK3/Image
	rm getutsrel
	rm utsrelease.c
	# clean out folder
	rm -rf out
	# revert back to do.modules=0
	sed -i "s/do\.modules=.*/do.modules=0/" "$(pwd)/AnyKernel3/anykernel.sh"
	rm -rf $AK3/modules/vendor/lib/modules/*.ko
	echo "stub" > $AK3/modules/vendor/lib/modules/stub
}	

post_build() {
	DATE=$(date +'%Y%m%d%H%M%S')
	if [ -d $(pwd)/.git ]; then
		GITSHA=$(git rev-parse --short HEAD)
	else
		GITSHA="localbuild"
	fi
	ZIP="AnyKernel3-`echo $DEVICE`_$GITSHA-$DATE"
	if [ -d $AK3 ]; then
		echo "- Creating AnyKernel3"
		gcc -CC utsrelease.c -o getutsrel
		UTSRELEASE=$(./getutsrel)
		sed -i "s/kernel\.string=.*/kernel.string=$UTSRELEASE/" "$(pwd)/AnyKernel3/anykernel.sh"
		cp $IMAGE $AK3
		cd $AK3
		zip -r9 ../`echo $ZIP`.zip *
		# CI will clean itself post-build, so we don't need to clean
		# Also avoid small AnyKernel3 zip issue!
		if [ $IS_CI != "true" ]; then
			if [[ "$POST_BUILD_CLEAN" = "true" ]]; then
				echo "- Host is not Automated CI, cleaning dirs"
				post_build_clean;
			fi
		fi
	fi
}

# build target
if [ "$BUILD" = "kernel" ]; then
	make -j`echo $ALLOC_JOB` -C $(pwd) O=$(pwd)/out `echo $DEFAULT_ARGS` `echo $BUILD_DEFCONFIG`
	if [ "$KERNELSU" = "true" ]; then		
    		setconfig enable KSU
	fi
	if [[ "$LTO" = "thin" ]]; then
		echo "LTO: thin"
		setconfig disable LTO_NONE
		setconfig enable LTO
		setconfig enable THINLTO
		setconfig enable LTO_CLANG
		setconfig enable ARCH_SUPPORTS_LTO_CLANG
		setconfig enable ARCH_SUPPORTS_THINLTO
	elif [[ "$LTO" = "full" ]]; then
		echo "LTO: full"
		setconfig disable LTO_NONE
		setconfig enable LTO
		setconfig disable THINLTO
		setconfig enable LTO_CLANG
		setconfig enable ARCH_SUPPORTS_LTO_CLANG
		setconfig enable ARCH_SUPPORTS_THINLTO
	else
		echo "LTO: none"
		setconfig enable LTO_NONE
		setconfig disable LTO
		setconfig disable THINLTO
		setconfig disable LTO_CLANG
		setconfig enable ARCH_SUPPORTS_LTO_CLANG
		setconfig enable ARCH_SUPPORTS_THINLTO
	fi
	make -j`echo $ALLOC_JOB` -C $(pwd) O=$(pwd)/out `echo $DEFAULT_ARGS`
	if [ -e $IMAGE ]; then
		pr_post_build "completed"
		post_build
	else
		pr_post_build "failed"
	fi
elif [ "$BUILD" = "defconfig" ]; then
	make -j`echo $ALLOC_JOB` -C $(pwd) O=$(pwd)/out `echo $DEFAULT_ARGS` `echo $BUILD_DEFCONFIG`
fi
