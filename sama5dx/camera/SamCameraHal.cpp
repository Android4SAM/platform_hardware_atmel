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

#include "SamCameraFactoryWrapper.h"

static hw_module_t camera_common = {
    tag: HARDWARE_MODULE_TAG,
    module_api_version: CAMERA_MODULE_API_VERSION_2_0,
    hal_api_version: HARDWARE_HAL_API_VERSION,
    id: CAMERA_HARDWARE_MODULE_ID,
    name: "Atmel Camera Module",
    author: "Atmel Corporation Inc",
    methods: &android::SamCameraFactoryWrapper::mCameraModuleMethods,
    dso: NULL,
    reserved:  {0},
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: camera_common,
    get_number_of_cameras: android::SamCameraFactoryWrapper::get_number_of_cameras,
    get_camera_info:android::SamCameraFactoryWrapper::get_camera_info,
    set_callbacks: NULL,
    get_vendor_tag_ops: NULL,
    reserved: {0}
};

