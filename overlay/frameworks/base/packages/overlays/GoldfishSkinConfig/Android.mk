#
#  Copyright 2023, The Android Open Source Project
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

# create skin related configuration files and symlinks
# 

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := GoldfishSkinConfig
LOCAL_MODULE_TAGS  := optional
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH  := $(TARGET_OUT_DATA)/misc/
LOCAL_SRC_FILES    := readme.txt
LOCAL_LICENSE_KINDS := SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice

# following is **needed**, as the framework only looks at vendor/etc/displayconfig
# that may change in the future, such as V or later
LOCAL_POST_INSTALL_CMD := ln -sf /data/system/displayconfig $(PRODUCT_OUT)/vendor/etc/displayconfig

# following is not needed, but keep a note here, as the framework already
# take /data/system/devicestate/ before accessing /vendor/etc/devicestate
# LOCAL_POST_INSTALL_CMD += ln -sf /data/system/devicestate $(PRODUCT_OUT)/vendor/etc/devicestate

include $(BUILD_PREBUILT)

