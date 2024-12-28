obj-$(CONFIG_KSU) += kernelsu.o

#ksu objs
kernelsu-objs := ksu.o
kernelsu-objs += allowlist.o
kernelsu-objs += apk_sign.o
kernelsu-objs += sucompat.o
kernelsu-objs += throne_tracker.o
kernelsu-objs += core_hook.o
kernelsu-objs += ksud.o
kernelsu-objs += embed_ksud.o
kernelsu-objs += kernel_compat.o
kernelsu-objs += selinux/selinux.o
kernelsu-objs += selinux/sepolicy.o
kernelsu-objs += selinux/rules.o
ccflags-y += -I$(srctree)/security/selinux -I$(srctree)/security/selinux/include
ccflags-y += -I$(objtree)/security/selinux -include $(srctree)/include/uapi/asm-generic/errno.h

ifeq ($(shell grep -q " current_sid(void)" $(srctree)/security/selinux/include/objsec.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_CURRENT_SID
endif

ifeq ($(shell grep -q "struct selinux_state " $(srctree)/security/selinux/include/security.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_SELINUX_STATE
endif

KSU_EXPECTED_SIZE_CUST := 0x3e6
KSU_EXPECTED_HASH_CUST := 79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7
KSU_EXPECTED_SIZE := 0x033b
KSU_EXPECTED_HASH := c371061b19d8c7d7d6133c6a9bafe198fa944e50c1b31c9d8daa8d7f1fc2d2d6

ccflags-y += -DEXPECTED_SIZE=$(KSU_EXPECTED_SIZE)
ccflags-y += -DEXPECTED_HASH=\"$(KSU_EXPECTED_HASH)\"
ccflags-y += -DEXPECTED_SIZE_CUST=$(KSU_EXPECTED_SIZE_CUST)
ccflags-y += -DEXPECTED_HASH_CUST=\"$(KSU_EXPECTED_HASH_CUST)\"

ccflags-y += -DKSU_VERSION=12072
ccflags-y += -DKSU_UMOUNT

ccflags-y += -Wno-implicit-function-declaration -Wno-strict-prototypes -Wno-int-conversion -Wno-gcc-compat
ccflags-y += -Wno-declaration-after-statement -Wno-unused-function

# Very Sussy ksu flags
ccflags-y += -DKSU_SUSFS
ccflags-y += -DKSU_SUSFS_SUS_PATH
ccflags-y += -DKSU_SUSFS_SUS_MOUNT
ccflags-y += -DKSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
ccflags-y += -DKSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
ccflags-y += -DKSU_SUSFS_SUS_KSTAT
ccflags-y += -DKSU_SUSFS_SUS_OVERLAYFS
ccflags-y += -DKSU_SUSFS_TRY_UMOUNT
ccflags-y += -DKSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
ccflags-y += -DKSU_SUSFS_SPOOF_UNAME
ccflags-y += -DKSU_SUSFS_OPEN_REDIRECT


ifdef KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
ccflags-y += -DKSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
endif
ifdef KSU_SUSFS_SPOOF_PROC_CMDLINE
ccflags-y += -DKSU_SUSFS_SPOOF_PROC_CMDLINE
endif
ifdef KSU_SUSFS_SUS_SU
ccflags-y += -DKSU_SUSFS_SUS_SU
endif

# Keep a new line here!! Because someone may append config
