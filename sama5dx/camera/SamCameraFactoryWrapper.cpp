/*
 * Copyright (C) 2014 Atmel Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "SamCameraFactoryWrapper"

#include <utils/Log.h>
#include <utils/Errors.h>
#include <utils/String8.h>
#include "hardwareloader.h"

#include "SamCameraFactoryWrapper.h"

extern camera_module_t HAL_MODULE_INFO_SYM;

/* A global instance of SamCameraFactoryWrapper is statically instantiated and
 * initialized when camera  HAL is loaded.
 */
android::SamCameraFactoryWrapper  gSamCameraFactoryWrapper;

namespace android {

SamCameraFactoryWrapper::SamCameraFactoryWrapper()
{
    sp<SamCameraFactory> factory = new SamCameraFactory();
    mHasRealHardwareCamera = factory->HasRealHardware();

    if(mHasRealHardwareCamera) {
        mRealCameraFactory = factory;
        ALOGD("we will use real hardware camera");
    } else {
        hw_get_module_by_class_directly(CAMERA_HARDWARE_MODULE_ID,
                                        "goldfish",
                                        (const hw_module_t **)&mModule);
        if(mModule == NULL)
            ALOGE("could not find any hardware modules");
        else
            ALOGD("we will use fake camera");
    }
}

SamCameraFactoryWrapper::~SamCameraFactoryWrapper()
{

}

int SamCameraFactoryWrapper::cameraDeviceOpen(int camera_id, hw_device_t** device)
{
    ALOGV("%s: id = %d", __FUNCTION__, camera_id);
    String8 deviceName = String8::format("%d", camera_id);
    *device = NULL;

    if(mHasRealHardwareCamera) {
        return mRealCameraFactory->camera_device_open(camera_id, device);
    }

    if(mModule != NULL) {
        mModule->common.methods->open(&mModule->common, deviceName,
            reinterpret_cast<hw_device_t**>(device));
        return 0;
    }

    return -1;
}

int SamCameraFactoryWrapper::getCameraInfo(int camera_id, struct camera_info* info)
{
    if(mHasRealHardwareCamera)
        return mRealCameraFactory->get_camera_info(camera_id, info);

    if(mModule != NULL) {
        return mModule->get_camera_info(camera_id, info);
    }

    return -1;
}

int SamCameraFactoryWrapper::getNumOfCameras()
{
    if(mHasRealHardwareCamera)
        return mRealCameraFactory->get_number_of_cameras();

    if(mModule != NULL) {
        return mModule->get_number_of_cameras();
    }

    return -1;
}

/****************************************************************************
 * Camera HAL API callbacks.
 ***************************************************************************/
int SamCameraFactoryWrapper::device_open(const hw_module_t* module,
                                       const char* name,
                                       hw_device_t** device)
{
    /*
     * Simply verify the parameters, and dispatch the call inside the
     * SamCameraFactoryWrapper instance.
     */

    if (module != &HAL_MODULE_INFO_SYM.common) {
        ALOGE("%s: Invalid module %p expected %p",
             __FUNCTION__, module, &HAL_MODULE_INFO_SYM.common);
        return -1;
    }
    if (name == NULL) {
        ALOGE("%s: NULL name is not expected here", __FUNCTION__);
        return -1;
    }

    return gSamCameraFactoryWrapper.cameraDeviceOpen(atoi(name), device);
}

int SamCameraFactoryWrapper::get_number_of_cameras(void)
{
    return gSamCameraFactoryWrapper.getNumOfCameras();
}

int SamCameraFactoryWrapper::get_camera_info(int camera_id,
                                           struct camera_info* info)
{
    return gSamCameraFactoryWrapper.getCameraInfo(camera_id, info);
}

/********************************************************************************
 * Initializer for the static member structure.
 *******************************************************************************/

/* Entry point for camera HAL API. */
struct hw_module_methods_t SamCameraFactoryWrapper::mCameraModuleMethods = {
    open: SamCameraFactoryWrapper::device_open
};

};
