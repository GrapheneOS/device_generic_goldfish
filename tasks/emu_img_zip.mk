# Rules to generate a zip file that contains google emulator images
# and other files for distribution

ifneq ($(filter sdk_% gcar_%, $(TARGET_PRODUCT)),)
target_notice_file_txt := $(TARGET_OUT_INTERMEDIATES)/NOTICE.txt

emulator_img_source_prop := $(TARGET_OUT_INTERMEDIATES)/source.properties
$(emulator_img_source_prop): $(PRODUCT_SDK_ADDON_SYS_IMG_SOURCE_PROP)
	$(process_prop_template)

ifeq ($(TARGET_ARCH), x86)
# a 32bit guest on a 64bit kernel
EMULATOR_KERNEL_DIST_NAME := kernel-ranchu-64
else
EMULATOR_KERNEL_DIST_NAME := kernel-ranchu
endif # x86

INTERNAL_EMULATOR_PACKAGE_FILES := \
	$(target_notice_file_txt) \
	$(emulator_img_source_prop) \
	$(PRODUCT_OUT)/system/build.prop \
	$(PRODUCT_OUT)/VerifiedBootParams.textproto \
	$(PRODUCT_OUT)/advancedFeatures.ini \
	$(PRODUCT_OUT)/$(EMULATOR_KERNEL_DIST_NAME) \
	$(PRODUCT_OUT)/kernel_cmdline.txt \
	$(PRODUCT_OUT)/encryptionkey.img \

ifneq ($(filter $(TARGET_PRODUCT), sdk_goog3_x86 sdk_goog3_x86_64 sdk_goog3_x86_arm),)
INTERNAL_EMULATOR_PACKAGE_FILES += \
	$(HOST_OUT_EXECUTABLES)/dex2oat \
	$(HOST_OUT_EXECUTABLES)/dex2oatd
endif

ifneq ($(filter $(PRODUCT_DEVICE), emulator_car64_arm64 emulator_car64_x86_64),)
INTERNAL_EMULATOR_PACKAGE_FILES += \
	hardware/interfaces/automotive/vehicle/aidl/emu_metadata/android.hardware.automotive.vehicle-types-meta.json
endif

INTERNAL_EMULATOR_PACKAGE_SOURCE := $(PRODUCT_OUT)/emulator
INTERNAL_EMULATOR_PACKAGE_SOURCE_DST := $(INTERNAL_EMULATOR_PACKAGE_SOURCE)/$(TARGET_CPU_ABI)
INTERNAL_EMULATOR_PACKAGE_TARGET := $(PRODUCT_OUT)/sdk-repo-linux-system-images.zip

INSTALLED_QEMU_SYSTEMIMAGE := $(PRODUCT_OUT)/system-qemu.img
INSTALLED_QEMU_RAMDISKIMAGE := $(PRODUCT_OUT)/ramdisk-qemu.img
INSTALLED_QEMU_VENDORIMAGE := $(PRODUCT_OUT)/vendor-qemu.img

PRODUCT_OUT_DATA_FILES := $(PRODUCT_OUT)/userdata.img # also builds $(PRODUCT_OUT)/data

INTERNAL_EMULATOR_PACKAGE_TARGET_DEPENDENCIES := \
	$(INTERNAL_EMULATOR_PACKAGE_FILES) \
	$(INSTALLED_QEMU_SYSTEMIMAGE) \
	$(INSTALLED_QEMU_RAMDISKIMAGE) \
	$(INSTALLED_QEMU_VENDORIMAGE) \
	$(PRODUCT_OUT_DATA_FILES) \
	$(ACP) $(SOONG_ZIP) \

$(INTERNAL_EMULATOR_PACKAGE_TARGET): $(INTERNAL_EMULATOR_PACKAGE_TARGET_DEPENDENCIES)
	@echo "Package: $@"
	$(hide) rm -rf $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)
	$(hide) mkdir -p $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)
	$(hide) $(foreach f,$(INTERNAL_EMULATOR_PACKAGE_FILES), $(ACP) $(f) $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)/$(notdir $(f));)
	$(hide) $(ACP) -r $(INSTALLED_QEMU_SYSTEMIMAGE) $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)/system.img
	$(hide) $(ACP) -r $(INSTALLED_QEMU_RAMDISKIMAGE) $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)/ramdisk.img
	$(hide) $(ACP) -r $(INSTALLED_QEMU_VENDORIMAGE) $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)/vendor.img
	$(hide) $(ACP) -r $(PRODUCT_OUT)/data $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)
	$(hide) $(SOONG_ZIP) -o $@ -C $(INTERNAL_EMULATOR_PACKAGE_SOURCE) -D $(INTERNAL_EMULATOR_PACKAGE_SOURCE_DST)

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
	$(PRODUCT_OUT)/system-qemu-config.txt \
	$(PRODUCT_OUT)/misc_info.txt \
	$(PRODUCT_OUT)/vbmeta.img \
	$(foreach p,$(BOARD_SUPER_PARTITION_PARTITION_LIST),$(PRODUCT_OUT)/$(p).img)

EMU_EXTRA_TARGET_DEPENDENCIES := \
	$(EMU_EXTRA_FILES) \
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
	$(hide) $(ACP) -r $(PRODUCT_OUT)/data $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)
	$(SOONG_ZIP) -o $@ -C $(PRIVATE_PACKAGE_SRC) -D $(PRIVATE_PACKAGE_SRC)/$(TARGET_ARCH)

.PHONY: emu_extra_imgs
emu_extra_imgs: $(EMU_EXTRA_TARGET)

$(call dist-for-goals-with-filenametag, emu_extra_imgs, $(EMU_EXTRA_TARGET))
endif
