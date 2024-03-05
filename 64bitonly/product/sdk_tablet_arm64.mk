#
# Copyright (C) 2023 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
PRODUCT_USE_DYNAMIC_PARTITIONS := true
EMULATOR_DISABLE_RADIO := true

# 2.5 GiB
BOARD_EMULATOR_DYNAMIC_PARTITIONS_SIZE ?= $(shell expr 2560 \* 1048576 )
BOARD_SUPER_PARTITION_SIZE := $(shell expr $(BOARD_EMULATOR_DYNAMIC_PARTITIONS_SIZE) + 8388608 )  # +8M

PRODUCT_COPY_FILES += \
    device/generic/goldfish/tablet/data/etc/display_settings.xml:$(TARGET_COPY_OUT_VENDOR)/etc/display_settings.xml \
    device/generic/goldfish/data/etc/advancedFeatures.ini.tablet:advancedFeatures.ini \
    device/generic/goldfish/data/etc/config.ini.nexus7tab:config.ini

PRODUCT_COPY_FILES+= \
        device/generic/goldfish/data/etc/tablet_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

PRODUCT_COPY_FILES += device/generic/goldfish/tablet/data/etc/tablet.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/tablet.xml

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)

PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := relaxed

PRODUCT_SDK_ADDON_SYS_IMG_SOURCE_PROP := \
    device/generic/goldfish/64bitonly/product/tablet_images_arm64-v8a_source.prop_template

$(call inherit-product, device/generic/goldfish/board/emu64a/details.mk)
$(call inherit-product, device/generic/goldfish/product/tablet.mk)

PRODUCT_BRAND := Android
PRODUCT_NAME := sdk_tablet_arm64
PRODUCT_DEVICE := emu64a
PRODUCT_MODEL := Android SDK Tablet for arm64
