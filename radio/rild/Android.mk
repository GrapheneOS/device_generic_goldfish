# Copyright 2006 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	rild_goldfish.c

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libdl \
	liblog \
	libril-modem-lib

# Temporary hack for broken vendor RILs.
LOCAL_WHOLE_STATIC_LIBRARIES := \
	librilutils-goldfish-fork

LOCAL_CFLAGS := -DRIL_SHLIB
LOCAL_CFLAGS += -Wall -Wextra -Werror

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
#LOCAL_MODULE:= rild
LOCAL_MODULE:= libgoldfish-rild
LOCAL_LICENSE_KINDS:= SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS:= notice
LOCAL_NOTICE_FILE:= $(LOCAL_PATH)/NOTICE
LOCAL_OVERRIDES_PACKAGES := rild
PACKAGES.$(LOCAL_MODULE).OVERRIDES := rild
LOCAL_INIT_RC := rild_goldfish.rc
LOCAL_CFLAGS += -DPRODUCT_COMPATIBLE_PROPERTY

include $(BUILD_EXECUTABLE)
