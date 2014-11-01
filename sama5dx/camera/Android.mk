# Copyright (C) 2011 The Android Open Source Project
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


LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

LOCAL_SHARED_LIBRARIES:= \
    libbinder \
    liblog \
    libutils \
    libcutils \
    libcamera_client \
    libui \
    libdl \
    libhardware

# JPEG conversion libraries and includes.
LOCAL_SHARED_LIBRARIES += \
    libjpeg \
    libcamera_metadata \
    libhardwareloader

LOCAL_C_INCLUDES += external/jpeg \
    frameworks/native/include/media/hardware \
    hardware/atmel/sama5dx/hardwareloader \
    hardware/atmel/sama5dx/debug \
    $(call include-path-for, camera)

LOCAL_SRC_FILES := \
    SamCameraHal.cpp \
    SamCameraFactoryWrapper.cpp \
    SamCameraFactory.cpp \
    SamCameraBase.cpp \
    SamCameraProtocol1.cpp \
    SamCamera.cpp \
    SamSensorBase.cpp \
    SamSensor.cpp \
    ColorConvert.cpp \
    PreviewWindow.cpp \
    CallbackNotifier.cpp \
    JpegCompressor.cpp \
    v4l2_utils.cpp

LOCAL_MODULE := camera.$(TARGET_BOOTLOADER_BOARD_NAME)

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_CFLAGS += -fno-short-enums
LOCAL_SHARED_LIBRARIES:= \
    libcutils \
    liblog \
    libskia \
    libandroid_runtime

LOCAL_C_INCLUDES += external/jpeg \
                    external/skia/include/core/ \
                    frameworks/base/core/jni/android/graphics \
                    frameworks/native/include

LOCAL_SRC_FILES := JpegSoftwareEncoder.cpp

LOCAL_MODULE := camera.softwareencoder.jpeg

include $(BUILD_SHARED_LIBRARY)
