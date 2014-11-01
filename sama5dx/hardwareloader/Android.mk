# Copyright 2014 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libcutils liblog

LOCAL_INCLUDES += $(LOCAL_PATH)

LOCAL_SHARED_LIBRARIES += libdl

LOCAL_SRC_FILES += hardwareloader.c

LOCAL_MODULE:= libhardwareloader

include $(BUILD_SHARED_LIBRARY)
