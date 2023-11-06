include device/generic/goldfish/arm64-kernel.mk

PRODUCT_PROPERTY_OVERRIDES += \
       vendor.rild.libpath=/vendor/lib64/libgoldfish-ril.so

# Note: the following lines need to stay at the beginning so that it can
# take priority  and override the rules it inherit from other mk files
# see copy file rules in core/Makefile
PRODUCT_COPY_FILES += \
    device/generic/goldfish/fstab.ranchu.arm:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.ranchu \
    device/generic/goldfish/fstab.ranchu.arm:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.ranchu \
    device/generic/goldfish/data/etc/encryptionkey.img:encryptionkey.img \
    $(EMULATOR_KERNEL_FILE):kernel-ranchu \
    device/generic/goldfish/data/etc/advancedFeatures.ini.arm:advancedFeatures.ini \
    device/generic/goldfish/camera/media/codecs_performance_c2_arm64.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_performance_c2.xml \

