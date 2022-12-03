TARGET_KERNEL_USE ?= 5.15

SYSTEM_KERNEL_MODULES_INCLUDE := \
    bluetooth.ko \
    btbcm.ko \
    can-dev.ko \
    libarc4.ko \
    rfkill.ko \

SYSTEM_KERNEL_MODULES := \
    $(foreach _ko,$(SYSTEM_KERNEL_MODULES_INCLUDE),\
        kernel/prebuilts/$(TARGET_KERNEL_USE)/x86_64/$(_ko))
VENDOR_KERNEL_MODULES := \
    $(wildcard kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/x86-64/*.ko)

BOARD_VENDOR_RAMDISK_KERNEL_MODULES += \
    $(SYSTEM_KERNEL_MODULES) \
    $(VENDOR_KERNEL_MODULES)

EMULATOR_KERNEL_FILE := kernel/prebuilts/$(TARGET_KERNEL_USE)/x86_64/kernel-$(TARGET_KERNEL_USE)
