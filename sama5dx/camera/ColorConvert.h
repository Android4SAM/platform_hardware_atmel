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

#ifndef HW_CAMERA_CONVERTERS_H
#define HW_CAMERA_CONVERTERS_H

#include <utils/Errors.h>
#include "SamSensorBase.h"
#include <camera/CameraParameters.h>
#include <utils/String8.h>

namespace android {

class SamSensorBase;

class ColorConvert {
    public:
        ColorConvert();
        ~ColorConvert();

        inline void setSensor(SamSensorBase* sensor_base)
        {
            mSensorBase = sensor_base;
        }
        
        inline void setDstFormat(const char* pix_fmt)
        {
            mDstFormat = pix_fmt;
        }

        inline int getDstFrameSize()
        {
            return mDstFrameSize;
        }
        
        bool     isValid();

        status_t convert(const void* srcBuf, void* dstBuf);

    private:
        void yuyv422_to_yuv420(unsigned char *bufsrc,
                               unsigned char *bufdest,
                               int width,
                               int height);
        
        SamSensorBase* mSensorBase;
        uint32_t       mSrcFormat;
        String8        mDstFormat;

        int            mSrcWidth;
        int            mSrcHeight;
        int            mSrcFrameSize;

        int            mDstFrameSize;

        enum CONVERT_METHOD {
            CONVERT_422_420P,
            CONVERT_MEMCPY,
        };

        CONVERT_METHOD mMethod;
        
};

}; /* namespace android */

#endif  /* HW_CAMERA_CONVERTERS_H */
