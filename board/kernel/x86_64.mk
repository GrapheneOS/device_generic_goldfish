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

# we do NOT support OTA - suppress the build warning
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

TARGET_KERNEL_USE ?= 6.1
KERNEL_ARTIFACTS_PATH := kernel/prebuilts/$(TARGET_KERNEL_USE)/x86_64

BOARD_DO_NOT_STRIP_RECOVERY_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_RAMDISK_MODULES := true
BOARD_DO_NOT_STRIP_VENDOR_KERNEL_RAMDISK_MODULES := true

VIRTUAL_DEVICE_KERNEL_MODULES_PATH := \
    kernel/prebuilts/common-modules/virtual-device/$(TARGET_KERNEL_USE)/x86-64

# The list of modules to reach the second stage. For performance reasons we
# don't want to put all modules into the ramdisk.
RAMDISK_KERNEL_MODULES := \
    virtio_blk.ko \
    virtio_console.ko \
    virtio_dma_buf.ko \
    virtio_pci.ko \
    virtio_pci_legacy_dev.ko \
    virtio_pci_modern_dev.ko \
    virtio-rng.ko \
    vmw_vsock_virtio_transport.ko \

BOARD_SYSTEM_KERNEL_MODULES := $(wildcard $(KERNEL_ARTIFACTS_PATH)/*.ko)

BOARD_VENDOR_RAMDISK_KERNEL_MODULES := \
    $(patsubst %,$(VIRTUAL_DEVICE_KERNEL_MODULES_PATH)/%,$(RAMDISK_KERNEL_MODULES))

BOARD_VENDOR_KERNEL_MODULES := \
    $(filter-out $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES),\
                 $(wildcard $(VIRTUAL_DEVICE_KERNEL_MODULES_PATH)/*.ko))

BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := \
    device/generic/goldfish/board/kernel/kernel_modules.blocklist

EMULATOR_KERNEL_FILE := $(KERNEL_ARTIFACTS_PATH)/kernel-$(TARGET_KERNEL_USE)
