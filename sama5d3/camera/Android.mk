LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES:=               \
    CameraHardwareSam.cpp					\
    V4L2Camera.cpp              \
    ccrgb16toyuv420.cpp

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.$(TARGET_BOOTLOADER_BOARD_NAME)

LOCAL_C_INCLUDES += external/jpeg

ifeq ($(TARGET_SIMULATOR),true)
LOCAL_CFLAGS += -DSINGLE_PROCESS

endif

LOCAL_SHARED_LIBRARIES:= 		\
				libui		\
				libutils \
				libbinder \
				libcutils \
				libjpeg   \
				libhardware \
				libcamera_client

include $(BUILD_SHARED_LIBRARY)
