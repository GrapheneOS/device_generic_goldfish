#
# Copyright (C) 2024 The Android Open Source Project
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

$(call inherit-product, device/generic/goldfish/product/base_handheld.mk)
# include webview
$(call inherit-product, $(SRC_TARGET_DIR)/product/media_product.mk)
# don't include full handheld_system_Ext which includes SystemUi, Settings etc
$(call inherit-product, $(SRC_TARGET_DIR)/product/media_system_ext.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_system_ext.mk)

DEVICE_PACKAGE_OVERLAYS += device/generic/goldfish/phone/overlay
PRODUCT_CHARACTERISTICS := emulator

# Include FakeSystemApp which replaces core system apps like Settings,
# Launcher
# and include the overlay that overrides systemui definitions with fakesystemapp
PRODUCT_PACKAGES += \
    FakeSystemApp \
    slim_overlay_frameworks_base_core

$(call inherit-product, device/generic/goldfish/product/generic.mk)

