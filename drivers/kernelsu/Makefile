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

obj-$(CONFIG_KSU) += kernelsu.o

# hardcode!
$(eval KSU_VERSION=11986)
$(info -- KernelSU version: $(KSU_VERSION))
ccflags-y += -DKSU_VERSION=$(KSU_VERSION)

ifeq ($(shell grep -q " current_sid(void)" $(srctree)/security/selinux/include/objsec.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_CURRENT_SID
endif

ifeq ($(shell grep -q "struct selinux_state " $(srctree)/security/selinux/include/security.h; echo $$?),0)
ccflags-y += -DKSU_COMPAT_HAS_SELINUX_STATE
endif

KSU_EXPECTED_SIZE := 0x033b
KSU_EXPECTED_HASH := c371061b19d8c7d7d6133c6a9bafe198fa944e50c1b31c9d8daa8d7f1fc2d2d6

ifndef KSU_EXPECTED_SIZE
KSU_EXPECTED_SIZE := 0x3cb
endif

ifndef KSU_EXPECTED_HASH
KSU_EXPECTED_HASH := a66a220227b7b539ace80a02f03e1e11cf5d20f66e9eaf02ef81dc9811946af2
endif

ifdef KSU_MANAGER_PACKAGE
ccflags-y += -DKSU_MANAGER_PACKAGE=\"$(KSU_MANAGER_PACKAGE)\"
$(info -- KernelSU Manager package name: $(KSU_MANAGER_PACKAGE))
endif

ccflags-y += -DEXPECTED_SIZE=$(KSU_EXPECTED_SIZE)
ccflags-y += -DEXPECTED_HASH=\"$(KSU_EXPECTED_HASH)\"
ccflags-y += -DKSU_UMOUNT

ccflags-y += -Wno-implicit-function-declaration -Wno-strict-prototypes -Wno-int-conversion -Wno-gcc-compat
ccflags-y += -Wno-declaration-after-statement -Wno-unused-function

ifdef KSU_SUSFS
ccflags-y += -DKSU_SUSFS
endif
ifdef KSU_SUSFS_SUS_PATH
ccflags-y += -DKSU_SUSFS_SUS_PATH
endif
ifdef KSU_SUSFS_SUS_MOUNT
ccflags-y += -DKSU_SUSFS_SUS_MOUNT
endif
ifdef KSU_SUSFS_SUS_MOUNT_MNT_ID_REORDER
ccflags-y += -DKSU_SUSFS_SUS_MOUNT_MNT_ID_REORDER
endif
ifdef KSU_SUSFS_SUS_KSTAT
ccflags-y += -DKSU_SUSFS_SUS_KSTAT
endif
ifdef KSU_SUSFS_SUS_MAPS
ccflags-y += -DKSU_SUSFS_SUS_MAPS
endif
ifdef KSU_SUSFS_SUS_PROC_FD_LINK
ccflags-y += -DKSU_SUSFS_SUS_PROC_FD_LINK
endif
ifdef KSU_SUSFS_SUS_MEMFD
ccflags-y += -DKSU_SUSFS_SUS_MEMFD
endif
ifdef KSU_SUSFS_TRY_UMOUNT
ccflags-y += -DKSU_SUSFS_TRY_UMOUNT
endif
ifdef KSU_SUSFS_SPOOF_UNAME
ccflags-y += -DKSU_SUSFS_SPOOF_UNAME
endif
ifdef KSU_SUSFS_ENABLE_LOG
ccflags-y += -DKSU_SUSFS_ENABLE_LOG
endif
# Keep a new line here!! Because someone may append config
