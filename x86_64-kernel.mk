TARGET_KERNEL_USE ?= 5.10

KERNEL_MODULES_PATH := kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/x86-64

KERNEL_MODULES_EXCLUDE := \
    $(KERNEL_MODULES_PATH)/ac97_bus.ko \
    $(KERNEL_MODULES_PATH)/gnss-cmdline.ko \
    $(KERNEL_MODULES_PATH)/gnss-serial.ko \
    $(KERNEL_MODULES_PATH)/pulse8-cec.ko \
    $(KERNEL_MODULES_PATH)/snd-ac97-codec.ko \
    $(KERNEL_MODULES_PATH)/snd-intel8x0.ko \
    $(KERNEL_MODULES_PATH)/tpm.ko \
    $(KERNEL_MODULES_PATH)/tpm_vtpm_proxy.ko \
    $(KERNEL_MODULES_PATH)/virt_wifi.ko \
    $(KERNEL_MODULES_PATH)/virt_wifi_sim.ko \
    $(KERNEL_MODULES_PATH)/virtiofs.ko

BOARD_VENDOR_RAMDISK_KERNEL_MODULES += \
    $(filter-out $(KERNEL_MODULES_EXCLUDE), $(wildcard $(KERNEL_MODULES_PATH)/*.ko))

EMULATOR_KERNEL_FILE := kernel/prebuilts/$(TARGET_KERNEL_USE)/x86_64/kernel-$(TARGET_KERNEL_USE)
