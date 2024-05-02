#
# Copyright (C) 2012 The Android Open Source Project
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

$(call inherit-product, device/generic/goldfish/product/handheld.mk)

DEVICE_PACKAGE_OVERLAYS += device/generic/goldfish/tablet/overlay
PRODUCT_CHARACTERISTICS := tablet,nosdcard

PRODUCT_PRODUCT_PROPERTIES += \
    ro.config.ringtone?=Ring_Synth_04.ogg \
    ro.config.notification_sound?=pixiedust.ogg \

PRODUCT_PACKAGES += \
    initial-package-stopped-states-aosp.xml \
    PhotoTable \
    preinstalled-packages-platform-aosp-product.xml \
    WallpaperPicker \
    LargeScreenSettingsProviderOverlay \
    curl \

PRODUCT_ARTIFACT_PATH_REQUIREMENT_ALLOWED_LIST += system/bin/curl

$(call inherit-product, device/generic/goldfish/product/generic.mk)
