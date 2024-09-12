# Rules to generate a zip file that contains google emulator images
# and other files for distribution

ifneq ($(filter sdk_% gcar_%, $(TARGET_PRODUCT)),)
emulator_img_source_prop := $(TARGET_OUT_INTERMEDIATES)/source.properties
target_notice_file_txt := $(TARGET_OUT_INTERMEDIATES)/NOTICE.txt
$(emulator_img_source_prop): $(PRODUCT_SDK_ADDON_SYS_IMG_SOURCE_PROP)
	$(process_prop_template)

INTERNAL_EMULATOR_PACKAGE_FILES := \
        $(target_notice_file_txt) \
        $(emulator_img_source_prop) \
        $(PRODUCT_OUT)/system/build.prop \

ifneq ($(filter $(TARGET_PRODUCT), sdk_goog3_x86 sdk_goog3_x86_64 sdk_goog3_x86_arm),)
    INTERNAL_EMULATOR_PACKAGE_FILES += \
        $(HOST_OUT_EXECUTABLES)/dex2oat \
        $(HOST_OUT_EXECUTABLES)/dex2oatd
endif

ifeq ($(BUILD_QEMU_IMAGES),true)
ifeq ($(BOARD_AVB_ENABLE),true)
INTERNAL_EMULATOR_PACKAGE_FILES += \
        $(PRODUCT_OUT)/VerifiedBootParams.textproto
endif
endif

INTERNAL_EMULATOR_PACKAGE_SOURCE := $(PRODUCT_OUT)/emulator

INSTALLED_QEMU_SYSTEMIMAGE := $(PRODUCT_OUT)/system-qemu.img
FINAL_INSTALLED_QEMU_SYSTEMIMAGE := $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)/system.img
$(eval $(call copy-one-file,$(INSTALLED_QEMU_SYSTEMIMAGE),$(FINAL_INSTALLED_QEMU_SYSTEMIMAGE)))

INSTALLED_QEMU_RAMDISKIMAGE := $(PRODUCT_OUT)/ramdisk-qemu.img
FINAL_INSTALLED_QEMU_RAMDISKIMAGE := $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)/ramdisk.img
$(eval $(call copy-one-file,$(INSTALLED_QEMU_RAMDISKIMAGE),$(FINAL_INSTALLED_QEMU_RAMDISKIMAGE)))

INSTALLED_QEMU_VENDORIMAGE := $(PRODUCT_OUT)/vendor-qemu.img
FINAL_INSTALLED_QEMU_VENDORIMAGE := $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)/vendor.img
$(eval $(call copy-one-file,$(INSTALLED_QEMU_VENDORIMAGE),$(FINAL_INSTALLED_QEMU_VENDORIMAGE)))


INTERNAL_EMULATOR_PACKAGE_FILES += device/generic/goldfish/data/etc/encryptionkey.img

ifneq ($(filter $(PRODUCT_DEVICE), emulator_car64_arm64 emulator_car64_x86_64),)
INTERNAL_EMULATOR_PACKAGE_FILES += hardware/interfaces/automotive/vehicle/aidl/emu_metadata/android.hardware.automotive.vehicle-types-meta.json
endif

name := sdk-repo-linux-system-images


INTERNAL_EMULATOR_PACKAGE_TARGET := $(PRODUCT_OUT)/$(name).zip

ifeq ($(TARGET_ARCH), arm)
# This is wrong and should be retired.
EMULATOR_KERNEL_FILE := prebuilts/qemu-kernel/arm/3.18/kernel-qemu2
EMULATOR_KERNEL_DIST_NAME := kernel-ranchu
else
ifeq ($(TARGET_ARCH), x86)
# Use 64-bit kernel even for 32-bit Android
EMULATOR_KERNEL_DIST_NAME := kernel-ranchu-64
else
# All other arches are 64-bit
EMULATOR_KERNEL_DIST_NAME := kernel-ranchu
endif # x86
endif # arm

PRODUCT_OUT_DATA_FILES := $(PRODUCT_OUT)/userdata.img # also builds $(PRODUCT_OUT)/data

INTERNAL_EMULATOR_PACKAGE_TARGET_DEPENDENCIES := \
	$(INTERNAL_EMULATOR_PACKAGE_FILES) \
	$(FINAL_INSTALLED_QEMU_SYSTEMIMAGE) \
	$(FINAL_INSTALLED_QEMU_RAMDISKIMAGE) \
	$(FINAL_INSTALLED_QEMU_VENDORIMAGE) \
	$(EMULATOR_KERNEL_FILE) \
	$(PRODUCT_OUT)/advancedFeatures.ini \
	$(PRODUCT_OUT)/kernel_cmdline.txt \
	$(PRODUCT_OUT_DATA_FILES) \

$(INTERNAL_EMULATOR_PACKAGE_TARGET): $(INTERNAL_EMULATOR_PACKAGE_TARGET_DEPENDENCIES)
	@echo "Package: $@"
	$(hide) mkdir -p $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)
	$(hide) $(foreach f,$(INTERNAL_EMULATOR_PACKAGE_FILES), $(ACP) $(f) $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)/$(notdir $(f));)
	$(hide) $(ACP) $(PRODUCT_OUT)/advancedFeatures.ini $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)
	$(hide) $(ACP) $(PRODUCT_OUT)/kernel_cmdline.txt $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)
	$(hide) ($(ACP) $(EMULATOR_KERNEL_FILE) $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)/${EMULATOR_KERNEL_DIST_NAME})
	$(hide) $(ACP) -r $(PRODUCT_OUT)/data $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)
	$(hide) $(SOONG_ZIP) -o $@ -C $(INTERNAL_EMULATOR_PACKAGE_SOURCE) -D $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)

.PHONY: emu_img_zip
emu_img_zip: $(INTERNAL_EMULATOR_PACKAGE_TARGET)

# TODO(b/361152997): replace goog_emu_imgs with emu_img_zip and retire this target
.PHONY: goog_emu_imgs
goog_emu_imgs: emu_img_zip

# The following rules generate emu_extra_imgs package. It is similar to
# emu_img_zip, but it does not contain system-qemu.img and vendor-qemu.img. It
# conatins the necessary data to build the qemu images. The package can be
# mixed with generic system, kernel, and system_dlkm images.
EMU_EXTRA_FILES := \
        $(INTERNAL_EMULATOR_PACKAGE_FILES) \
        $(INSTALLED_QEMU_RAMDISKIMAGE) \
        $(PRODUCT_OUT)/advancedFeatures.ini \
        $(PRODUCT_OUT)/kernel_cmdline.txt \
        $(PRODUCT_OUT)/system-qemu-config.txt \
        $(PRODUCT_OUT)/misc_info.txt \
        $(PRODUCT_OUT)/vbmeta.img \
        $(foreach p,$(BOARD_SUPER_PARTITION_PARTITION_LIST),$(PRODUCT_OUT)/$(p).img)

EMU_EXTRA_TARGET_DEPENDENCIES := \
        $(EMU_EXTRA_FILES) \
        $(EMULATOR_KERNEL_FILE) \
        $(ADVANCED_FEATURES_FILES) \
        $(PRODUCT_OUT_DATA_FILES)

EMU_EXTRA_TARGET := $(PRODUCT_OUT)/emu-extra-linux-system-images.zip

$(EMU_EXTRA_TARGET): PRIVATE_PACKAGE_SRC := \
        $(call intermediates-dir-for, PACKAGING, emu_extra_target)

$(EMU_EXTRA_TARGET): $(EMU_EXTRA_TARGET_DEPENDENCIES) $(SOONG_ZIP)
	@echo "Package: $@"
	$(hide) rm -rf $@ $(PRIVATE_PACKAGE_SRC)
	$(hide) mkdir -p $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)/system
	$(hide) $(ACP) $(PRODUCT_OUT)/system/build.prop $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)/system
	$(hide) $(foreach f,$(EMU_EXTRA_FILES), $(ACP) $(f) $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)/$(notdir $(f)) &&) true
	$(hide) $(ACP) $(EMULATOR_KERNEL_FILE) $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)/${EMULATOR_KERNEL_DIST_NAME}
	$(hide) $(ACP) -r $(PRODUCT_OUT)/data $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)
	$(SOONG_ZIP) -o $@ -C $(PRIVATE_PACKAGE_SRC) -D $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)

.PHONY: emu_extra_imgs
emu_extra_imgs: $(EMU_EXTRA_TARGET)

$(call dist-for-goals-with-filenametag, emu_extra_imgs, $(EMU_EXTRA_TARGET))
endif
