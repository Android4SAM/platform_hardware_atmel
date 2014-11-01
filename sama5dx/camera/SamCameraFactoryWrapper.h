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

#ifndef __SAMCAMERAFACTORYWRAPPER_H__
#define __SAMCAMERAFACTORYWRAPPER_H__

#include "SamCameraFactory.h"

namespace android {

class SamCameraFactoryWrapper
{
public:
    SamCameraFactoryWrapper();
    ~SamCameraFactoryWrapper();
public:

    /****************************************************************************
        * Camera HAL API handlers.
        ***************************************************************************/

    /* Opens (connects to) a camera device.
         * This method is called in response to hw_module_methods_t::open callback.
         */
    int cameraDeviceOpen(int camera_id, hw_device_t** device);

    /* Gets camera information.
         * This method is called in response to camera_module_t::get_camera_info callback.
         */
    int getCameraInfo(int camera_id, struct camera_info *info);

    /* Gets NumOfCameras.
         */
    int getNumOfCameras(void);

public:
    /* camera_module_t::get_number_of_cameras callback entry point. */
    static int get_number_of_cameras(void);

    /* camera_module_t::get_camera_info callback entry point. */
    static int get_camera_info(int camera_id, struct camera_info *info);

private:
    /* hw_module_methods_t::open callback entry point. */
    static int device_open(const hw_module_t* module,
                               const char* name,
                               hw_device_t** device);

    camera_module_t *mModule;
    sp<SamCameraFactory> mRealCameraFactory;
    bool  mHasRealHardwareCamera;
public:
    /* Contains device open entry point, as required by HAL API. */
    static struct hw_module_methods_t   mCameraModuleMethods;
};

};

#endif /* __SAMCAMERAFACTORYWRAPPER_H__ */
