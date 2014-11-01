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
 * Contains implemenation of framebuffer conversion routines.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "Camera_Converter"
#include <cutils/log.h>
#include "ColorConvert.h"

namespace android {

ColorConvert::ColorConvert()
    : mSensorBase(NULL),
      mSrcFormat(0),
      mDstFormat(""),
      mSrcWidth(0),
      mSrcHeight(0),
      mSrcFrameSize(0)
{
}

ColorConvert::~ColorConvert()
{
}

bool ColorConvert::isValid()
{
    if (mSensorBase == NULL) {
        ALOGE("mSensorBase is null");
        return false;
    }

    mSrcFormat = mSensorBase->getOriginalPixelFormat();

    if (mSrcFormat != V4L2_PIX_FMT_YUYV) {
        ALOGE("error SrcFormat, %d", mSrcFormat);
        return false;
    }

    mSrcWidth = mSensorBase->getFrameWidth();
    mSrcHeight = mSensorBase->getFrameHeight();
    mSrcFrameSize = mSensorBase->getFrameBufferSize();

    if (mDstFormat != CameraParameters::PIXEL_FORMAT_YUV420P) {
        ALOGE("error DstFormat, %s", mDstFormat.string());
        return false;
    } else {
        mMethod = CONVERT_422_420P;
        mDstFrameSize = (mSrcWidth * mSrcHeight * 12) / 8;
    }

    return true;
}

status_t ColorConvert::convert(const void * srcBuf,void * dstBuf)
{
    switch (mMethod) {
        case CONVERT_422_420P:
            yuyv422_to_yuv420((unsigned char *)srcBuf,
                               (unsigned char *)dstBuf,
                               mSrcWidth,
                               mSrcHeight);
            break;
        default:
            ALOGE("Cannot convert date");
            dstBuf = NULL;
            return BAD_VALUE;
    }
    return NO_ERROR;
}

void ColorConvert::yuyv422_to_yuv420(unsigned char * bufsrc,
                                     unsigned char * bufdest,
                                     int width,
                                     int height)
{
    unsigned char *ptrsrcy1, *ptrsrcy2;
    unsigned char *ptrsrcy3, *ptrsrcy4;
    unsigned char *ptrsrccb1, *ptrsrccb2;
    unsigned char *ptrsrccb3, *ptrsrccb4;
    unsigned char *ptrsrccr1, *ptrsrccr2;
    unsigned char *ptrsrccr3, *ptrsrccr4;
    int srcystride, srcccstride;

    ptrsrcy1  = bufsrc ;
    ptrsrcy2  = bufsrc + (width<<1) ;
    ptrsrcy3  = bufsrc + (width<<1)*2 ;
    ptrsrcy4  = bufsrc + (width<<1)*3 ;

    ptrsrccb1 = bufsrc + 1;
    ptrsrccb2 = bufsrc + (width<<1) + 1;
    ptrsrccb3 = bufsrc + (width<<1)*2 + 1;
    ptrsrccb4 = bufsrc + (width<<1)*3 + 1;

    ptrsrccr1 = bufsrc + 3;
    ptrsrccr2 = bufsrc + (width<<1) + 3;
    ptrsrccr3 = bufsrc + (width<<1)*2 + 3;
    ptrsrccr4 = bufsrc + (width<<1)*3 + 3;

    srcystride  = (width<<1)*3;
    srcccstride = (width<<1)*3;

    unsigned char *ptrdesty1, *ptrdesty2;
    unsigned char *ptrdesty3, *ptrdesty4;
    unsigned char *ptrdestcb1, *ptrdestcb2;
    unsigned char *ptrdestcr1, *ptrdestcr2;
    int destystride, destccstride;

    ptrdesty1 = bufdest;
    ptrdesty2 = bufdest + width;
    ptrdesty3 = bufdest + width*2;
    ptrdesty4 = bufdest + width*3;

    ptrdestcb1 = bufdest + width*height;
    ptrdestcb2 = bufdest + width*height + (width>>1);

    ptrdestcr1 = bufdest + width*height + ((width*height) >> 2);
    ptrdestcr2 = bufdest + width*height + ((width*height) >> 2) + (width>>1);

    destystride  = (width)*3;
    destccstride = (width>>1);

    int i, j;

    for(j=0; j<(height/4); j++)
    {
        for(i=0;i<(width/2);i++)
        {
            (*ptrdesty1++) = (*ptrsrcy1);
            (*ptrdesty2++) = (*ptrsrcy2);
            (*ptrdesty3++) = (*ptrsrcy3);
            (*ptrdesty4++) = (*ptrsrcy4);

            ptrsrcy1 += 2;
            ptrsrcy2 += 2;
            ptrsrcy3 += 2;
            ptrsrcy4 += 2;

            (*ptrdesty1++) = (*ptrsrcy1);
            (*ptrdesty2++) = (*ptrsrcy2);
            (*ptrdesty3++) = (*ptrsrcy3);
            (*ptrdesty4++) = (*ptrsrcy4);

            ptrsrcy1 += 2;
            ptrsrcy2 += 2;
            ptrsrcy3 += 2;
            ptrsrcy4 += 2;

            (*ptrdestcb1++) = (*ptrsrccb1);
            (*ptrdestcb2++) = (*ptrsrccb3);

            ptrsrccb1 += 4;
            ptrsrccb3 += 4;

            (*ptrdestcr1++) = (*ptrsrccr1);
            (*ptrdestcr2++) = (*ptrsrccr3);

            ptrsrccr1 += 4;
            ptrsrccr3 += 4;

        }


        /* Update src pointers */
        ptrsrcy1  += srcystride;
        ptrsrcy2  += srcystride;
        ptrsrcy3  += srcystride;
        ptrsrcy4  += srcystride;

        ptrsrccb1 += srcccstride;
        ptrsrccb3 += srcccstride;

        ptrsrccr1 += srcccstride;
        ptrsrccr3 += srcccstride;


        /* Update dest pointers */
        ptrdesty1 += destystride;
        ptrdesty2 += destystride;
        ptrdesty3 += destystride;
        ptrdesty4 += destystride;

        ptrdestcb1 += destccstride;
        ptrdestcb2 += destccstride;

        ptrdestcr1 += destccstride;
        ptrdestcr2 += destccstride;

    }
}
}; /* namespace android */
