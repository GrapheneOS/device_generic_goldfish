#
# Copyright (C) 2021 The Android Open Source Project
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
# This is a build configuration for the 'slim' image targeted
# for headless automated testing. Compared to the full AOSP 'sdk_phone'
# image it removes/replaces most product apps, and turns off rendering
# by default.

# Enable mainline checking for this exact product name
ifeq (sdk_slim_arm64,$(TARGET_PRODUCT))
PRODUCT_ENFORCE_ARTIFACT_PATH_REQUIREMENTS := relaxed
endif

# 2.5 GiB
BOARD_EMULATOR_DYNAMIC_PARTITIONS_SIZE ?= $(shell expr 2560 \* 1048576 )
BOARD_SUPER_PARTITION_SIZE := $(shell expr $(BOARD_EMULATOR_DYNAMIC_PARTITIONS_SIZE) + 8388608 )  # +8M

$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)

PRODUCT_SDK_ADDON_SYS_IMG_SOURCE_PROP := \
    development/sys-img/images_atd_source.prop_template

# this must go first - overwrites the goldfish handheld_core_hardware.xml
$(call inherit-product, device/generic/goldfish/slim/vendor.mk)
$(call inherit-product, device/generic/goldfish/board/emu64a/details.mk)
$(call inherit-product, device/generic/goldfish/product/slim_handheld.mk)

PRODUCT_BRAND := Android
PRODUCT_NAME := sdk_slim_arm64
PRODUCT_DEVICE := emu64a
PRODUCT_MODEL := Android ATD built for arm64
