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

TARGET_KERNEL_USE ?= 6.6
KERNEL_ARTIFACTS_PATH := prebuilts/qemu-kernel/x86_64/$(TARGET_KERNEL_USE)
KERNEL_MODULES_ARTIFACTS_PATH := $(KERNEL_ARTIFACTS_PATH)/gki_modules
VIRTUAL_DEVICE_KERNEL_MODULES_PATH := $(KERNEL_ARTIFACTS_PATH)/goldfish_modules

# The list of modules to reach the second stage. For performance reasons we
# don't want to put all modules into the ramdisk.
RAMDISK_KERNEL_MODULES := \
    virtio_dma_buf.ko \
    virtio-rng.ko \

RAMDISK_SYSTEM_KERNEL_MODULES := \
    virtio_blk.ko \
    virtio_console.ko \
    virtio_pci.ko \
    virtio_pci_legacy_dev.ko \
    virtio_pci_modern_dev.ko \
    vmw_vsock_virtio_transport.ko \

BOARD_SYSTEM_KERNEL_MODULES := $(wildcard $(KERNEL_MODULES_ARTIFACTS_PATH)/*.ko)

BOARD_VENDOR_RAMDISK_KERNEL_MODULES := \
    $(wildcard $(patsubst %,$(VIRTUAL_DEVICE_KERNEL_MODULES_PATH)/%,$(RAMDISK_KERNEL_MODULES))) \
    $(wildcard $(patsubst %,$(KERNEL_MODULES_ARTIFACTS_PATH)/%,$(RAMDISK_SYSTEM_KERNEL_MODULES)))

BOARD_VENDOR_KERNEL_MODULES := \
    $(filter-out $(BOARD_VENDOR_RAMDISK_KERNEL_MODULES),\
                 $(wildcard $(VIRTUAL_DEVICE_KERNEL_MODULES_PATH)/*.ko))

BOARD_VENDOR_KERNEL_MODULES_BLOCKLIST_FILE := \
    device/generic/goldfish/board/kernel/kernel_modules.blocklist

EMULATOR_KERNEL_FILE := $(KERNEL_ARTIFACTS_PATH)/kernel-$(TARGET_KERNEL_USE)

# BOARD_KERNEL_CMDLINE is not supported (b/361341981), use the file below
PRODUCT_COPY_FILES += \
    device/generic/goldfish/board/kernel/x86_64_cmdline.txt:kernel_cmdline.txt
