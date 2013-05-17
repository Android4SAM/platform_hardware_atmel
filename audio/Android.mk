# Copyright 2011 The Android Open Source Project

#AUDIO_POLICY_TEST := true
#ENABLE_AUDIO_DUMP := true

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    AudioHardwareInterface.cpp \
    audio_hw_hal.cpp

LOCAL_MODULE := libaudiohw_atmel
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libmedia_helper

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    AudioPolicyManagerBase.cpp \
    AudioPolicyCompatClient.cpp \
    audio_policy_hal.cpp

ifeq ($(AUDIO_POLICY_TEST),true)
  LOCAL_CFLAGS += -DAUDIO_POLICY_TEST
endif

include $(CLEAR_VARS)

LOCAL_STATIC_LIBRARIES := libmedia_helper
LOCAL_MODULE := libaudiopolicy_atmel
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# The default audio policy, for now still implemented on top of legacy
# policy code
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    AudioPolicyManagerDefault.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libutils

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper

LOCAL_WHOLE_STATIC_LIBRARIES := \
    libaudiopolicy_atmel

LOCAL_MODULE := audio_policy.sama5d3-ek
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

#ifeq ($(ENABLE_AUDIO_DUMP),true)
#  LOCAL_SRC_FILES += AudioDumpInterface.cpp
#  LOCAL_CFLAGS += -DENABLE_AUDIO_DUMP
#endif
#
#ifeq ($(strip $(BOARD_USES_GENERIC_AUDIO)),true)
#  LOCAL_CFLAGS += -D GENERIC_AUDIO
#endif

#ifeq ($(BOARD_HAVE_BLUETOOTH),true)
#  LOCAL_SRC_FILES += A2dpAudioInterface.cpp
#  LOCAL_SHARED_LIBRARIES += liba2dp
#  LOCAL_C_INCLUDES += $(call include-path-for, bluez)
#
#  LOCAL_CFLAGS += \
#      -DWITH_BLUETOOTH \
#endif
#
#include $(BUILD_SHARED_LIBRARY)

#    AudioHardwareGeneric.cpp \
#    AudioHardwareStub.cpp \

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_CFLAGS := -D_POSIX_SOURCE

LOCAL_C_INCLUDES += $(TOPDIR)external/alsa-lib/include

LOCAL_SRC_FILES := \
    AudioHardwareALSA.cpp \
    AudioStreamInALSA.cpp \
    AudioStreamOutALSA.cpp \
    ALSAStreamOps.cpp \
    ALSAMixer.cpp \
    ALSAControl.cpp
LOCAL_SHARED_LIBRARIES := \
    libasound \
    libcutils \
    libutils \
    libmedia \
    libhardware \
    libhardware_legacy \
    libc 

LOCAL_STATIC_LIBRARIES := \
    libmedia_helper

LOCAL_WHOLE_STATIC_LIBRARIES := \
    libaudiohw_atmel \
    alsa.sama5d3-ek 

LOCAL_MODULE := audio.primary.sama5d3-ek
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# This is the default ALSA module which behaves closely like the original

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_CFLAGS := -D_POSIX_SOURCE -Wno-multichar

ifneq ($(ALSA_DEFAULT_SAMPLE_RATE),)
LOCAL_CFLAGS += -DALSA_DEFAULT_SAMPLE_RATE=$(ALSA_DEFAULT_SAMPLE_RATE)
endif

LOCAL_C_INCLUDES += $(TOPDIR)external/alsa-lib/include

LOCAL_SRC_FILES:= alsa_default.cpp

LOCAL_SHARED_LIBRARIES := \
	libasound \
  	liblog

LOCAL_MODULE:= alsa.sama5d3-ek

LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# This is the default Acoustics module which is essentially a stub

include $(CLEAR_VARS)

LOCAL_PRELINK_MODULE := false

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_CFLAGS := -D_POSIX_SOURCE -Wno-multichar

LOCAL_C_INCLUDES += $(TOPDIR)external/alsa-lib/include

LOCAL_SRC_FILES:= acoustics_default.cpp

LOCAL_SHARED_LIBRARIES := liblog

LOCAL_MODULE:= acoustics.sama5d3-ek

LOCAL_MODULE_TAGS := optional

