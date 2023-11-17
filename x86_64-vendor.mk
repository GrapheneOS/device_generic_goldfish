include device/generic/goldfish/board/kernel/x86_64.mk

PRODUCT_PROPERTY_OVERRIDES += \
       vendor.rild.libpath=/vendor/lib64/libgoldfish-ril.so

ADVANCED_FEATURES_FILE := advancedFeatures.ini
ifneq ($(filter %_minigbm, $(TARGET_PRODUCT)),)
ADVANCED_FEATURES_FILE := advancedFeatures.ini.minigbm
endif

# This is a build configuration for a full-featured build of the
# Open-Source part of the tree. It's geared toward a US-centric
# build quite specifically for the emulator, and might not be
# entirely appropriate to inherit from for on-device configurations.
PRODUCT_COPY_FILES += \
    device/generic/goldfish/data/etc/config.ini.xl:config.ini \
    device/generic/goldfish/data/etc/$(ADVANCED_FEATURES_FILE):advancedFeatures.ini \
    $(EMULATOR_KERNEL_FILE):kernel-ranchu \
    device/generic/goldfish/board/fstab/x86:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.ranchu \
    device/generic/goldfish/board/fstab/x86:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.ranchu
