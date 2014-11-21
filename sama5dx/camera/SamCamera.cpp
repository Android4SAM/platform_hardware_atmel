/*
 * Copyright (C) 2011 The Android Open Source Project
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

/*
 * Contains implementation of a class EmulatedFakeCamera that encapsulates
 * functionality of a fake camera.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "SamCamera"
#include <cutils/log.h>
#include <cutils/properties.h>
#include "SamCamera.h"


namespace android {

SamCamera::SamCamera(int cameraId,
                                       bool facingBack,
                                       struct hw_module_t* module)
        : SamCameraProtocol1(cameraId, module),
          mFacingBack(facingBack),
          mSamSensor(this)
{
    ALOGV("%s", __FUNCTION__);
}

SamCamera::~SamCamera()
{
    ALOGV("%s", __FUNCTION__);
}

bool SamCamera::hasRealHardware()
{
    return mSamSensor.hasRealHardware();
}

/****************************************************************************
 * Public API overrides
 ***************************************************************************/

status_t SamCamera::Initialize()
{
    ALOGV("%s", __FUNCTION__);

    status_t res = mSamSensor.Initialize();
    if (res != NO_ERROR) {
        return res;
    }

    const char* facing = mFacingBack ? SamCameraProtocol1::FACING_BACK :
                                       SamCameraProtocol1::FACING_FRONT;

    mParameters.set(SamCameraProtocol1::FACING_KEY, facing);
    ALOGD("%s: Sam camera is facing %s", __FUNCTION__, facing);

    mParameters.set(SamCameraProtocol1::ORIENTATION_KEY, 0);

    res = SamCameraProtocol1::Initialize();
    if (res != NO_ERROR) {
        return res;
    }

    /*
     * Parameters provided by the camera device.
     */

    /* 320x240 frame dimensions are required by the framework for
     * video mode preview and video recording. */
    mParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                    "640x480");
    mParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                    "640x480");
    mParameters.setPreviewSize(640, 480);
    mParameters.setPictureSize(640, 480);

    return NO_ERROR;
}

SamSensorBase* SamCamera::getCameraDevice()
{
    return &mSamSensor;
}

};  /* namespace android */
