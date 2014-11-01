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

//#define LOG_NDEBUG 0
#define LOG_TAG "SamCameraFactory"
#include <cutils/log.h>

#include "SamCameraFactory.h"

extern camera_module_t HAL_MODULE_INFO_SYM;

namespace android {

SamCameraFactory::SamCameraFactory()
    : mHasRealHardware(false),
      mNumOfCameras(0)
{
    ALOGV("%s", __FUNCTION__);

    status_t res;

    mNumOfCameras = getNumOfCameras();
    mCameraDevice = new SamCamera(0, true, &HAL_MODULE_INFO_SYM.common);
    mHasRealHardware = mCameraDevice->hasRealHardware();;

    if (mCameraDevice != NULL) {
        res = mCameraDevice->Initialize();
    }

}

SamCameraFactory::~SamCameraFactory()
{
    ALOGV("%s", __FUNCTION__);
}


/****************************************************************************
 * Camera HAL API handlers.
 *
 * Each handler simply verifies existence of an appropriate BaseCamera
 * instance, and dispatches the call to that instance.
 *
 ***************************************************************************/
int SamCameraFactory::get_number_of_cameras()
{
    ALOGV("%s", __FUNCTION__);
    return mNumOfCameras;
}

int SamCameraFactory::get_camera_info(int camera_id, struct camera_info* info)
{
    ALOGV("%s", __FUNCTION__);
    mCameraDevice->getCameraInfo(info);
    return 0;
}

int SamCameraFactory::camera_device_open(int camera_id, hw_device_t** device)
{
    ALOGV("%s", __FUNCTION__);
    mCameraDevice->connectCamera(device);
    return 0;
}

/****************************************************************************
 * Private functions.
 ***************************************************************************/
int SamCameraFactory::getNumOfCameras()
{
//We can check /dev/video and check the card.id
    return 1;
}


};
