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
#ifndef __SAMCAMERAFACTORY_H__
#define __SAMCAMERAFACTORY_H__

#include <hardware/camera.h>
#include <utils/RefBase.h>

#include "SamCamera.h"

namespace android {

class SamCameraFactory : public RefBase {
    public:
        SamCameraFactory();
        virtual ~SamCameraFactory();
        int camera_device_open(int camera_id, hw_device_t** device);
        int get_number_of_cameras();
        int get_camera_info(int camera_id, struct camera_info *info);
        bool HasRealHardware() {return mHasRealHardware;}
    private:
        int getNumOfCameras();
        bool mHasRealHardware;
        int mNumOfCameras;
        SamCamera* mCameraDevice;
};

};
#endif /* __SAMCAMERAFACTORY_H__ */